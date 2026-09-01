// GPU 拾取：把每个 draw 的 picking_id 写进一张 R32_UINT 的 ID 缓冲，
// 之后按像素读回来就知道点到了谁。
//
// 只有一个 uint 输出，没有颜色也没有光照 —— 拾取不需要知道东西长什么样。
// 遮挡靠 PickingPass 自己那张深度（每次拾取清成 1.0，Less 且写入）解决，所以只有最靠前的
// 那个片元会写下 ID。不复用 G-Buffer 的深度是因为那要求顶点变换和 gbuffer.slang 逐位一致，
// 而这里的 MVP 是 CPU 侧乘好的 —— 详见 picking_pass.cpp。

struct PickingConstants
{
    float4x4 model_view_projection;
    uint picking_id;
};

[[vk::push_constant]] ConstantBuffer<PickingConstants> constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(constants.model_view_projection, float4(input.position, 1.0));
    return output;
}

[shader("fragment")]
uint fragmentMain(VertexOutput input) : SV_Target0
{
    return constants.picking_id;
}
