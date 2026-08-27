// 把离屏的 SceneColor 贴到 backbuffer。全屏三角形，不需要顶点缓冲。

[[vk::binding(0, 0)]] Texture2D<float4> scene_color;
[[vk::binding(1, 0)]] SamplerState scene_sampler;

struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FullscreenVertexOutput vertexMain(uint vertex_id : SV_VertexID)
{
    const float2 uv = float2(float((vertex_id << 1) & 2), float(vertex_id & 2));
    FullscreenVertexOutput output;
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    return float4(scene_color.Sample(scene_sampler, input.uv).rgb, 1.0);
}
