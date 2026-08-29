// 天空：全屏三角形，按逆 view-projection 把屏幕像素反投影成一条视线方向，采环境 cube。
//
// 不画一个立方体网格是因为没必要 —— 一个覆盖全屏的三角形加深度测试就够，而且不用管
// 网格朝向、缩放、相机进到立方体里面这些事。
//
// 输出的是场景线性 HDR（乘上环境强度），写进 SceneColor，由 TonemapPass 统一压缩。

struct SkyConstants
{
    float4x4 inverse_view_projection;
    // xyz = 相机世界坐标，w = 环境强度倍率
    float4 camera_position_intensity;
};

[[vk::binding(0, 0)]] ConstantBuffer<SkyConstants> sky_constants;
[[vk::binding(1, 0)]] TextureCube<float4> environment_cube;
[[vk::binding(2, 0)]] SamplerState environment_sampler;

struct VertexOutput
{
    float4 position : SV_Position;
    // 反投影后的齐次坐标。透视除法留到片元里做 —— 在顶点里做再插值是错的（那是非线性的）。
    float4 world_homogeneous : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertex_id : SV_VertexID)
{
    const float2 uv = float2(float((vertex_id << 1) & 2), float(vertex_id & 2));
    VertexOutput output;
    // z = 1 是远平面（投影是 RH_ZO，深度范围 0..1）。配上 LessOrEqual 且不写深度，
    // 只有还是清屏值 1.0 的像素能通过 —— 也就是没被任何物体覆盖的地方。
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 1.0, 1.0);
    output.world_homogeneous = mul(sky_constants.inverse_view_projection, output.position);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float3 world_position = input.world_homogeneous.xyz / input.world_homogeneous.w;
    const float3 direction =
            normalize(world_position - sky_constants.camera_position_intensity.xyz);
    const float3 color = environment_cube.SampleLevel(environment_sampler, direction, 0.0).rgb *
                         sky_constants.camera_position_intensity.w;
    return float4(color, 1.0);
}
