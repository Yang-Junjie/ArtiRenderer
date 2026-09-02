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
    float4 ambient_color;
    // x = 环境强度倍率，y = IBL 是否就绪（>0.5 才走 IBL），z = prefiltered 的 mip 数
    float4 environment_params;
    // RenderScene::clear_color。没有几何体覆盖的像素直接输出它。
    float4 background_color;
    // x = 本帧的光源数量，yzw 未用。用 uint4 而不是塞进某个 float4 的空槽：数量是整数，
    // 而且 uint4 和 float4 在 cbuffer 里都是 16 字节对齐，混着放不影响打包规则。
    uint4 light_count;
};

// 一个光源。全部 float4，HLSL 和 std430 的打包规则因此一致，两侧不用对 padding 猜谜。
// CPU 侧的对应物是 deferred_lighting_pass.cpp 里的 GpuLight。
struct GpuLight
{
    // xyz = 世界坐标（方向光不用），w = 影响半径（方向光不用）
    float4 position_range;
    // xyz = **光的传播方向**，和 LightDesc::direction 同一个约定 —— 谁需要「从表面指向光源」
    // 谁自己取反。以前是 CPU 侧先取反再传，那在只有方向光时省事，但聚光的锥轴要的正是传播
    // 方向，两种含义挤在一个字段里迟早出错。
    // w = 类型：0 方向光，1 点光，2 聚光。
    float4 direction_type;
    // rgb = 颜色，a = 强度
    float4 color_intensity;
    // 聚光的角度衰减，CPU 侧从内外锥角预算好：x = scale，y = offset，
    // saturate(cos * scale + offset) 在内锥以内是 1、外锥以外是 0。zw 未用。
    float4 cone_scale_offset;
};

static const uint kLightDirectional = 0;
static const uint kLightPoint = 1;
static const uint kLightSpot = 2;

// 全部走 UBO 而不是 push constant：inverse_view_projection(64) + 四个 float4(64) + uint4(16)
// = 144 字节，远超 Vulkan 保证的 128。
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
// 本帧的光源列表。走 StructuredBuffer 而不是 cbuffer 里的定长数组：数量不设上限，
// 缓冲按需增长（见 deferred_lighting_pass.cpp）。没有光源的帧里它仍然是一个有效的
// 单元素缓冲 —— 空缓冲建不起 binding set，而 light_count 为 0 时循环一次都不进。
[[vk::binding(10, 0)]] StructuredBuffer<GpuLight> lights;

// 方向光的级联阴影。ShadowPass 每帧写好深度图和矩阵，这里采。
//
// 必须和 shadow_targets.h 的 kShadowCascadeCount 一致 —— Slang 要编译期常量。
static const uint kShadowCascadeCount = 4;

struct ShadowConstants
{
    // 世界空间 -> 第 i 级的光源裁剪空间。
    float4x4 light_view_projection[kShadowCascadeCount];
    // 每一级覆盖到相机前方多远（view-space 距离）。xyzw 依次对应四级。
    float4 split_far;
    // x = 阴影总距离，y = 淡出起点占总距离的比例，z = 一个 texel 在 UV 里的大小（1/分辨率），
    // w = 投影的那个光源在 lights[] 里的下标（>= light_count 表示这一帧没有阴影）。
    float4 params;
    // xyz = 相机前向（单位向量）。选 cascade 要「沿前向的距离」，而 LightingConstants 里只有
    // 相机位置和一个逆 view-projection —— 从后者反解前向又绕又脆，直接传。w 未用。
    float4 camera_forward;
};

[[vk::binding(11, 0)]] ConstantBuffer<ShadowConstants> shadow_constants;
// Texture2DArray：每一级一个 slice。用 array 而不是 atlas，所以每级的 UV 算法完全一样，
// 只多一个 slice 下标 —— 见 shadow_targets.h 顶部的说明。
[[vk::binding(12, 0)]] Texture2DArray<float> shadow_map;
// 普通采样器 + 着色端手动比较，**不用 SamplerComparisonState**：比较采样器走反射绑定路径
// 没验过，而手动 PCF 一定能过。以后想上硬件 PCF 只改这里和一个 sampler desc。
[[vk::binding(13, 0)]] SamplerState shadow_sampler;

static const float kPi = 3.14159265359;

// 方向光的阴影可见性：1 = 全亮，0 = 全遮。
//
// UV 的 Y 用 `0.5 - 0.5 * ndc.y` 而不是 `0.5 + 0.5 * ndc.y`：本项目跟 NVRHI 的 D3D 式屏幕约定
// （Vulkan 后端用负 viewport 高度），全屏三角形那边也是 `1 - uv.y * 2` 把 UV 映到 clip 的，
// 这里是它的逆。搞反的表现是阴影上下颠倒 —— 一眼能看出来。
float sampleDirectionalShadow(float3 world_position)
{
    // 选级用的深度是**沿相机前向的距离**，不是到相机的径向距离。前者等于 -(view * world).z，
    // 也正是 computeShadowCascades 里算分割距离用的那个量；用 length(camera - world) 会让屏幕
    // 边缘提前跳级 —— 同样的前向距离，边缘的径向距离更大。
    const float view_depth = dot(world_position - lighting_constants.camera_position.xyz,
            shadow_constants.camera_forward.xyz);

    const float shadow_distance = shadow_constants.params.x;
    // 超出阴影距离就没有阴影。
    if (view_depth >= shadow_distance)
    {
        return 1.0;
    }

    // 选第一个覆盖到这个深度的 cascade。都覆盖不到（浮点边界）就用最后一级兜底。
    uint cascade = kShadowCascadeCount - 1;
    for (uint level = 0; level < kShadowCascadeCount; ++level)
    {
        if (view_depth <= shadow_constants.split_far[level])
        {
            cascade = level;
            break;
        }
    }

    const float4 light_clip =
            mul(shadow_constants.light_view_projection[cascade], float4(world_position, 1.0));
    // 正交投影没有透视除法（w 恒为 1），除一下只是为了不依赖这个前提。
    const float3 ndc = light_clip.xyz / light_clip.w;

    // 落在这一级之外就当没有阴影。深度超出 [0,1] 同理 —— 那说明它在光锥的前后之外。
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
    {
        return 1.0;
    }

    // 3x3 PCF。手动比较再取平均，硬件比较采样器见绑定处的说明。
    const float texel = shadow_constants.params.z;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2(float(x), float(y)) * texel;
            const float occluder =
                    shadow_map.SampleLevel(shadow_sampler, float3(uv + offset, float(cascade)), 0.0);
            // 存的深度更小 = 那里有个更靠近光源的东西挡着。
            lit += ndc.z <= occluder ? 1.0 : 0.0;
        }
    }
    const float shadow = lit / 9.0;

    // 远端淡出：从 fade_start 开始把阴影往 1 抹，到 shadow_distance 正好抹平。不做的话最后一级
    // 的边界上会有一条硬边，而那条边会随相机移动扫过场景 —— 比没有阴影更显眼。
    const float fade_start = shadow_distance * shadow_constants.params.y;
    const float fade = saturate((view_depth - fade_start) / max(shadow_distance - fade_start, 1e-4));
    return lerp(shadow, 1.0, fade);
}

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

// 点光 / 聚光的距离衰减：物理的 1/d²，外面套一个在 range 处平滑归零的窗口。
//
// 纯 1/d² 永远不归零，光源就没有影响边界 —— 那样既没法做剔除，面板上改 range 也看不出效果。
// 窗口取 (1 - (d²/r²)²)² 而不是线性截断：它在 d = r 处值和一阶导都是 0，所以边界上看不到
// 一圈硬边。这是 Filament 的 getSquareFalloffAttenuation。
float distanceAttenuation(float distance_squared, float range)
{
    const float factor = distance_squared / max(range * range, 1e-4);
    const float window = saturate(1.0 - factor * factor);
    return window * window / max(distance_squared, 1e-4);
}

// 聚光的角度衰减。scale / offset 由 CPU 从内外锥角算好（见 deferred_lighting_pass.cpp），
// 所以这里只剩一次 mad + saturate。再平方一下让边缘过渡更柔和 —— 线性的看得出来是一圈直边。
float coneAttenuation(float cosine, float2 scale_offset)
{
    const float attenuation = saturate(cosine * scale_offset.x + scale_offset.y);
    return attenuation * attenuation;
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
    // nov 不能为 0：可见性项的分母里两个 lambda 都带它，同时为 0 会让镜面项炸到上限；
    // 它也是 DFG LUT 的采样坐标。逐光源的 nol 在下面的循环里算。
    const float nov = max(dot(normal, view_direction), 1e-4);
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

    // 直接光：把每个光源的贡献累加起来。延迟管线的收益就在这个循环里 —— 光源数和几何复杂度
    // 解耦了，加一个灯的代价是每个**可见像素**多跑一次 BRDF，而不是每个片元多跑一遍整条管线。
    //
    // 没有剔除：每个像素都要遍历全部光源。几个到几十个灯是够的，上百个才需要 tile / cluster
    // 把光源按屏幕区域分桶 —— 那一步要的是这个循环之前多一层索引，不用改这里的着色。
    float3 direct = float3(0.0, 0.0, 0.0);
    for (uint index = 0; index < lighting_constants.light_count.x; ++index)
    {
        const GpuLight light = lights[index];
        const uint type = uint(light.direction_type.w);

        // 从表面指向光源的单位向量 + 这个光源在这一点的衰减。
        float3 light_direction;
        float attenuation = 1.0;
        if (type == kLightDirectional)
        {
            // 方向光没有位置、不衰减。存的是传播方向，所以取反。
            light_direction = -light.direction_type.xyz;
        }
        else
        {
            const float3 to_light = light.position_range.xyz - world_position;
            const float distance_squared = dot(to_light, to_light);
            light_direction = to_light * rsqrt(max(distance_squared, 1e-8));
            attenuation = distanceAttenuation(distance_squared, light.position_range.w);
            if (type == kLightSpot)
            {
                // 锥轴就是传播方向，和「从表面指向光源」正好反向，所以这里取反再点乘。
                attenuation *= coneAttenuation(dot(light.direction_type.xyz, -light_direction),
                        light.cone_scale_offset.xy);
            }
        }

        const float nol = max(dot(normal, light_direction), 0.0);
        // 衰减到 0（超出 range 或在锥外）和背面朝光都没有贡献。提前跳掉省的是下面整段 BRDF。
        if (attenuation <= 0.0 || nol <= 0.0)
        {
            continue;
        }

        const float3 half_direction = normalize(view_direction + light_direction);
        const float noh = max(dot(normal, half_direction), 0.0);
        const float voh = max(dot(view_direction, half_direction), 0.0);

        const float3 fresnel = fresnelSchlick(f0, voh);
        const float3 specular = distributionGGX(noh, alpha) *
                visibilitySmithGGXCorrelated(nov, nol, alpha) * fresnel * energy_compensation;
        // 金属没有漫反射；(1 - fresnel) 是能量守恒 —— 反射掉的那部分不再进入次表面。
        const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / kPi;

        // 只有被 ShadowPass 选中的那个方向光有阴影图。下标由 ShadowTargets 传过来，
        // 不在这边重新推一遍「哪个灯投影」—— 两处各推一次就会漂。
        float shadow = 1.0;
        if (type == kLightDirectional && index == uint(shadow_constants.params.w))
        {
            shadow = sampleDirectionalShadow(world_position);
        }

        const float3 radiance = light.color_intensity.rgb * light.color_intensity.a;
        direct += (diffuse + specular) * radiance * nol * attenuation * shadow;
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
