// 调试线。世界空间的线段列表，逐顶点带颜色，画在 DisplayColor 上。
//
// 颜色**原样输出**：这个 pass 排在 TonemapPass 之后，写的是显示线性的 DisplayColor，
// 所以调用方给的颜色就是屏幕上看到的颜色。调试绘制的全部意义就在这里 —— 用红/绿/蓝表示
// 不同含义时，不希望曝光和 tone 曲线把它们改掉。
//
// 顶点属性的 location 由 DebugLinePass 建 input layout 时给定，和这里的顺序对应：
//   0 position  1 color

struct DrawConstants
{
    // 只有一个矩阵，64 字节，稳稳在 Vulkan 保证的 128 以内。
    float4x4 view_projection;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw_constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    // nointerpolation：一条线段两个端点的颜色目前总是相同的，插值只是白算。
    // 想做渐变线的话把这个去掉就行。
    nointerpolation float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(draw_constants.view_projection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return input.color;
}
