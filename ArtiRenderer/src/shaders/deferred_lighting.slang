// 延迟光照：一个覆盖全屏的三角形，读 G-Buffer + 深度，把光算出来写进 SceneColor（场景线性 HDR）。
//
// G-Buffer 的通道分配定义在 gbuffer.slang 顶部，这里必须逐通道对齐。两处的分工是：写入侧只存
// 材质属性（albedo / metallic / roughness / 法线 / emissive / occlusion），所有 BRDF 求值都在
// 这一个 shader 里 —— 这正是延迟渲染的意义，着色和几何解耦了。
//
// **不给这个 pass 的 framebuffer 挂深度附件**：世界坐标要从深度反投影出来，也就是深度必须当
// SRV 采。同一张图既是深度附件又是采样源在 Vulkan 里是 feedback loop。所以「这个像素有没有
// 几何体」是靠采回来的深度自己判断的，不靠深度测试 —— 反正重建坐标本来就要采那一次深度。
//
// 没有几何体的像素输出 background_color（也就是 RenderScene::clear_color），**不是 discard**：
//   1. Slang 的 discard 会编译成 OpDemoteToHelperInvocation，那需要 Vulkan 1.3 的
//      shaderDemoteToHelperInvocation 特性，当前设备没开；
//   2. 顺带也更省 —— demote 会让一些硬件放弃 early-Z 之类的优化。
// 代价是这个 pass 成了 SceneColor 每一个像素的唯一写入者（全屏三角形 + 无深度测试 + 无 discard），
// 所以 ClearScenePass 不再清 SceneColor：背景色只有一个来源，两处不需要「说好保持一致」。
// 有环境贴图时随后的 SkyPass 会把背景那些像素盖掉。

struct LightingConstants
{
    // 屏幕像素 -> 世界坐标。反投影的约定和 skybox.slang 一致（投影是 RH_ZO，深度 0..1）。
    float4x4 inverse_view_projection;
    float4 camera_position;
    // xyz = 从表面指向光源的单位向量。LightDesc::direction 存的是传播方向，CPU 侧已取反。
    float4 light_direction;
    // xyz = 颜色，w = 强度
    float4 light_color;
    float4 ambient_color;
    // x = 环境强度倍率，y = IBL 是否就绪（>0.5 才走 IBL），z = prefiltered 的 mip 数
    float4 environment_params;
    // RenderScene::clear_color。没有几何体覆盖的像素直接输出它。
    float4 background_color;
};

// 全部走 UBO 而不是 push constant：inverse_view_projection(64) + 六个 float4(96) = 160 字节，
// 远超 Vulkan 保证的 128。
[[vk::binding(0, 0)]] ConstantBuffer<LightingConstants> lighting_constants;
[[vk::binding(1, 0)]] Texture2D<float4> g_albedo_metallic;
[[vk::binding(2, 0)]] Texture2D<float4> g_normal_roughness;
[[vk::binding(3, 0)]] Texture2D<float4> g_emissive_occlusion;
[[vk::binding(4, 0)]] Texture2D<float> g_depth;
// **必须是 point 采样**。G-Buffer 和这个 pass 同分辨率、1:1 对应，线性过滤在这里没有意义，
// 但会在几何边缘把两侧的法线和材质插值混起来 —— 混出来的既不是这边的表面也不是那边的。
[[vk::binding(5, 0)]] SamplerState gbuffer_sampler;
// IBL 三件套，由 EnvironmentBakePass 烘出来。没有环境贴图时它们是 1x1 的黑图（不是空句柄，
// 否则 binding set 建不起来），此时 environment_params.y 是 0，着色走常数环境项那条分支。
[[vk::binding(6, 0)]] TextureCube<float4> irradiance_cube;
[[vk::binding(7, 0)]] TextureCube<float4> prefiltered_cube;
[[vk::binding(8, 0)]] Texture2D<float4> brdf_lut;
[[vk::binding(9, 0)]] SamplerState ibl_sampler;

static const float kPi = 3.14159265359;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id : SV_VertexID)
{
    const float2 uv = float2(float((vertex_id << 1) & 2), float(vertex_id & 2));
    VertexOutput output;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}

float3 reconstructWorldPosition(float2 uv, float depth)
{
    const float4 ndc = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    const float4 world = mul(lighting_constants.inverse_view_projection, ndc);
    // 透视除法必须在这里做而不是在顶点阶段 —— 插值齐次坐标是非线性的。
    return world.xyz / world.w;
}

// GGX / Trowbridge-Reitz 法线分布。
//
// alpha 由调用方算好传进来（alpha = roughness^2，Disney 那套「感知线性」的约定），不在函数
// 里算：D 和 V 必须用同一个 alpha，各算一次早晚会有人只改一处。
float distributionGGX(float noh, float alpha)
{
    const float alpha_squared = alpha * alpha;
    const float denominator = noh * noh * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(kPi * denominator * denominator, 1e-6);
}

// 高度相关的 Smith-GGX 可见性项（Filament 的 V_SmithGGXCorrelated）。
//
// 返回的是 **visibility** 而不是几何项 G —— 1/(4·NoV·NoL) 已经折进去了，所以镜面项写成
// D * V * F，没有那次显式除法（原来那句 `/ max(4.0 * nov * nol, 1e-4)` 因此消失了）。
//
// 换掉原来分离式的 Schlick-GGX（k = (r+1)^2/8，UE4/LearnOpenGL 那一支）的理由：那个形式把
// masking 和 shadowing 当成独立事件，而它们在同一个微表面场上是相关的 —— 视线和光线都掠射
// 时尤其明显。这里是精确形式而不是 Filament 的 fast 近似：分母那两个 sqrt 在延迟管线里
// 一个像素只算一次，省不出什么。
//
// IBL 那条路不用这个函数：它的 Smith 项（k = a/2）已经烘进 BRDF LUT 了，见 brdf_lut.slang。
float visibilitySmithGGXCorrelated(float nov, float nol, float alpha)
{
    const float alpha_squared = alpha * alpha;
    const float lambda_v = nol * sqrt(nov * nov * (1.0 - alpha_squared) + alpha_squared);
    const float lambda_l = nov * sqrt(nol * nol * (1.0 - alpha_squared) + alpha_squared);
    return 0.5 / max(lambda_v + lambda_l, 1e-5);
}

float3 fresnelSchlick(float3 f0, float voh)
{
    return f0 + (1.0 - f0) * pow(1.0 - voh, 5.0);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float depth = g_depth.Sample(gbuffer_sampler, input.uv);
    // 深度还是清屏值（1.0 = 远平面），说明这个像素没有任何几何体覆盖 —— 输出背景色。
    // 为什么不是 discard，见文件顶部。
    if (depth >= 1.0)
    {
        return lighting_constants.background_color;
    }

    const float4 albedo_metallic = g_albedo_metallic.Sample(gbuffer_sampler, input.uv);
    const float4 normal_roughness = g_normal_roughness.Sample(gbuffer_sampler, input.uv);
    const float4 emissive_occlusion = g_emissive_occlusion.Sample(gbuffer_sampler, input.uv);

    const float3 emissive = emissive_occlusion.rgb;
    const float3 albedo = albedo_metallic.rgb;
    const float metallic = saturate(albedo_metallic.a);
    // 写入侧已经归一化过了，这里再来一次是因为 16F 存取和光栅化插值都会让长度略微漂掉。
    const float3 normal = normalize(normal_roughness.xyz);
    // 下限和写入侧一致：再低下去 GGX 的高光会退化成一个亮度爆掉的点。
    const float roughness = clamp(normal_roughness.w, 0.045, 1.0);
    const float occlusion = saturate(emissive_occlusion.a);

    const float3 world_position = reconstructWorldPosition(input.uv, depth);
    const float3 view_direction =
            normalize(lighting_constants.camera_position.xyz - world_position);
    const float3 light_direction = normalize(lighting_constants.light_direction.xyz);
    // nov 不能为 0：可见性项的分母里两个 lambda 都带它，同时为 0 会让镜面项炸到上限；
    // 它也是 DFG LUT 的采样坐标。
    const float nov = max(dot(normal, view_direction), 1e-4);
    const float nol = max(dot(normal, light_direction), 0.0);
    // 介质的垂直反射率固定 0.04，金属用自己的 albedo 当 F0 —— metallic 工作流的全部内容。
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float alpha = roughness * roughness;

    // split-sum 的 DFG 项。在 IBL 分支**之外**采，因为下面的能量补偿两条路都要用，而 LUT 只
    // 依赖 (NoV, roughness)、和有没有环境贴图无关（EnvironmentBakePass 第一帧就烘好它，
    // 不等第一张 HDR）。
    const float2 dfg = brdf_lut.SampleLevel(ibl_sampler, float2(nov, roughness), 0.0).rg;

    // 多次散射的能量补偿。单次散射的 GGX 只算「进来一次、反射一次」，微表面之间来回弹的那部分
    // 直接丢了，所以粗糙金属会偏暗发灰 —— roughness 越大丢得越多。
    //
    // 补偿系数按这张 LUT 自己的拆分推：brdf_lut.slang 存的是 x = ∫(1-Fc)·Gv、y = ∫Fc·Gv，
    // 所以 f0 = 1（全反射）时的方向反射率就是 x + y，也就是单次散射真正留下的那部分能量。
    // 缺的是 1/(x+y)，按 f0 加权插值回去 —— 金属（f0 ≈ 0.9）补得多，介质（f0 = 0.04）几乎不补，
    // 这正是物理上该有的样子。
    //
    // **不要照抄 Filament 的 `1 + f0 * (1/dfg.y - 1)`**：那是按它自己 LUT 的通道语义写的，
    // 和这里的 (1-Fc)/Fc 拆分不是同一件事，直接搬会让粗糙金属亮到发白。
    //
    // 不给分母垫 epsilon 而是显式判 > 0：这个和为 0 只有「LUT 还没烘」一种可能（理论上不会
    // 发生，EnvironmentBakePass 第一帧就烘），那种情况下想要的是**不补偿**而不是补一个无穷。
    // 正常范围内它不会小到出问题 —— GGX 的单次散射方向反射率在整张 LUT 上都在 0.4 以上，
    // 所以补偿系数最多到 2.4 倍左右，不需要再夹一个上限。
    const float single_scatter = dfg.x + dfg.y;
    const float3 energy_compensation =
            single_scatter > 0.0 ? 1.0 + f0 * (1.0 / single_scatter - 1.0) : float3(1.0, 1.0, 1.0);

    float3 direct = float3(0.0, 0.0, 0.0);
    if (nol > 0.0)
    {
        const float3 half_direction = normalize(view_direction + light_direction);
        const float noh = max(dot(normal, half_direction), 0.0);
        const float voh = max(dot(view_direction, half_direction), 0.0);

        const float3 fresnel = fresnelSchlick(f0, voh);
        const float3 specular = distributionGGX(noh, alpha) *
                visibilitySmithGGXCorrelated(nov, nol, alpha) * fresnel * energy_compensation;
        // 金属没有漫反射；(1 - fresnel) 是能量守恒 —— 反射掉的那部分不再进入次表面。
        const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / kPi;

        const float3 radiance =
                lighting_constants.light_color.rgb * lighting_constants.light_color.w;
        direct = (diffuse + specular) * radiance * nol;
    }

    // 环境项。有 IBL 就走 split-sum，没有就退回常数环境光。
    float3 ambient_diffuse;
    float3 ambient_specular;
    if (lighting_constants.environment_params.y > 0.5)
    {
        const float environment_weight = lighting_constants.environment_params.x * occlusion;

        // 漫反射：辐照度图直接乘 albedo。**这里刻意不除 pi** —— irradiance.slang 产出的是
        // 真正的辐照度 E，1/pi 没折进去，两边是配对的约定（见那个文件顶部的说明）。
        const float3 irradiance = irradiance_cube.SampleLevel(ibl_sampler, normal, 0.0).rgb;

        // 镜面：split-sum。prefiltered 的 mip 对应 roughness，mip 数从 CPU 传进来而不是硬编码。
        const float3 reflection = reflect(-view_direction, normal);
        const float prefiltered_mip =
                roughness * max(lighting_constants.environment_params.z - 1.0, 0.0);
        const float3 prefiltered =
                prefiltered_cube.SampleLevel(ibl_sampler, reflection, prefiltered_mip).rgb;

        // 环境的 Fresnel 要带 roughness：粗糙表面的掠射角反射远不如光滑表面那么强，
        // 用直接光那个 fresnelSchlick 会让粗糙金属边缘出现一圈假亮边。
        const float3 ibl_fresnel =
                f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - nov, 5.0);

        ambient_diffuse =
                (1.0 - ibl_fresnel) * (1.0 - metallic) * albedo * irradiance * environment_weight;
        ambient_specular = prefiltered * (ibl_fresnel * dfg.x + dfg.y) * energy_compensation *
                environment_weight;
    }
    else
    {
        // 没有 IBL 时的占位：一个常数环境光，按 metallic 硬分成漫反射和镜面两份，而不是走
        // Fresnel（没有方向可言）。给金属留一份镜面是刻意的 —— 否则金属会是纯黑，
        // 看起来像 BRDF 写错了。
        const float3 ambient = lighting_constants.ambient_color.rgb * occlusion;
        ambient_diffuse = ambient * albedo * (1.0 - metallic);
        ambient_specular = ambient * f0;
    }

    // alpha 写 1：这条路径上的材质都是不透明的，SceneColor 的 alpha 下游也没有消费者。
    return float4(direct + ambient_diffuse + ambient_specular + emissive, 1.0);
}
