// Dear ImGui 的一帧 ImDrawData。顶点带自己的位置、UV 和颜色，投影靠 push constant 的
// scale/translate 完成，不需要矩阵。

[[vk::binding(0, 0)]] Texture2D<float4> gui_texture;
[[vk::binding(1, 0)]] SamplerState gui_sampler;

struct PushConstants
{
    float2 scale;
    float2 translate;
    // 逐 draw：字体图集这类 ImGui 自己的纹理存的是 sRGB 数值，要解码；SceneColor 是线性的离屏
    // 目标，不能再解。一个 flag 换掉两条管线。
    uint decode_srgb;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float3 sRGBToLinear(float3 value)
{
    return select(value <= 0.04045, value / 12.92, pow((value + 0.055) / 1.055, 2.4));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position * push_constants.scale + push_constants.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float4 color = input.color * gui_texture.Sample(gui_sampler, input.uv);
    // ImGui 的顶点色和字体图集都是 sRGB 数值，而 backbuffer 的 view 是 SRGBA8_UNORM ——
    // 硬件会在写入时替我们编码。所以这里要先解码回线性，一来一回抵消，UI 才是它本来的样子；
    // 少了这一步整个界面会发白。
    //
    // 采样的是 SceneColor（已经是线性）时跳过：那种情况顶点色是白的，乘完就是场景本身的值，
    // 再解一次会明显偏暗。
    if (push_constants.decode_srgb != 0)
    {
        color.rgb = sRGBToLinear(color.rgb);
    }
    return color;
}
