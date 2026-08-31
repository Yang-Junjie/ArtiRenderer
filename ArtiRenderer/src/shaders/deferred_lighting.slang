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

// GGX / Trowbridge-Reitz 法线分布。alpha = roughness^2 是 Disney 那套「感知线性」的约定。
float distributionGGX(float noh, float roughness)
{
    const float alpha = roughness * roughness;
    const float alpha_squared = alpha * alpha;
    const float denominator = noh * noh * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(kPi * denominator * denominator, 1e-6);
}

// Smith 几何项，Schlick-GGX 近似。k 用直接光的那个取值 (r+1)^2/8；IBL 那条路的 k 已经折进了
// BRDF LUT，所以这个函数只给直接光用。
float geometrySmith(float nov, float nol, float roughness)
{
    const float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    const float ggx_v = nov / lerp(nov, 1.0, k);
    const float ggx_l = nol / lerp(nol, 1.0, k);
    return ggx_v * ggx_l;
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
    // nov 不能为 0：它出现在 specular 的分母里。
    const float nov = max(dot(normal, view_direction), 1e-4);
    const float nol = max(dot(normal, light_direction), 0.0);
    // 介质的垂直反射率固定 0.04，金属用自己的 albedo 当 F0 —— metallic 工作流的全部内容。
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float3 direct = float3(0.0, 0.0, 0.0);
    if (nol > 0.0)
    {
        const float3 half_direction = normalize(view_direction + light_direction);
        const float noh = max(dot(normal, half_direction), 0.0);
        const float voh = max(dot(view_direction, half_direction), 0.0);

        const float3 fresnel = fresnelSchlick(f0, voh);
        const float3 specular = distributionGGX(noh, roughness) *
                geometrySmith(nov, nol, roughness) * fresnel / max(4.0 * nov * nol, 1e-4);
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
        const float2 brdf = brdf_lut.SampleLevel(ibl_sampler, float2(nov, roughness), 0.0).rg;

        // 环境的 Fresnel 要带 roughness：粗糙表面的掠射角反射远不如光滑表面那么强，
        // 用直接光那个 fresnelSchlick 会让粗糙金属边缘出现一圈假亮边。
        const float3 ibl_fresnel =
                f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - nov, 5.0);

        ambient_diffuse =
                (1.0 - ibl_fresnel) * (1.0 - metallic) * albedo * irradiance * environment_weight;
        ambient_specular = prefiltered * (ibl_fresnel * brdf.x + brdf.y) * environment_weight;
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
