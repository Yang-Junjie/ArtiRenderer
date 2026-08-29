// 把离屏的 DisplayColor 贴到 backbuffer。全屏三角形，不需要顶点缓冲。
//
// 采的是 tone mapping 之后的显示线性层，不是 SceneColor —— 这里只做搬运，不做任何色彩变换，
// sRGB 编码由 backbuffer 的 sRGB view 在写入时完成。

[[vk::binding(0, 0)]] Texture2D<float4> display_color;
[[vk::binding(1, 0)]] SamplerState display_sampler;

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
    return float4(display_color.Sample(display_sampler, input.uv).rgb, 1.0);
}
