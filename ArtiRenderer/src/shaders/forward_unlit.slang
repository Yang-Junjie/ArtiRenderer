// 前向 unlit 着色：base_color * base_color_texture，不参与光照。
//
// 顶点属性的 location 必须和 detail::meshVertexLayout() 的顺序一致
// （nvrhi Vulkan 后端按数组下标分配 location）：
//   0 position  1 normal  2 tangent  3 bitangent  4 uv
// 这里只声明用得到的那两个。
//
// 不需要逐帧 UBO：unlit 不读光照，view_projection 直接在 CPU 侧乘进 MVP。所以这个 shader 的
// binding layout 只有纹理和采样器两项，push constant 只有 80 字节。

struct DrawConstants
{
    float4x4 model_view_projection;
    float4 base_color;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw_constants;
[[vk::binding(0, 0)]] Texture2D<float4> base_color_texture;
[[vk::binding(1, 0)]] SamplerState base_color_sampler;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(4)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(draw_constants.model_view_projection, float4(input.position, 1.0));
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return base_color_texture.Sample(base_color_sampler, input.uv) * draw_constants.base_color;
}
