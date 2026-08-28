// GPU 拾取：把每个 draw 的 picking_id 写进一张 R32_UINT 的 ID 缓冲，
// 之后按像素读回来就知道点到了谁。
//
// 只有一个 uint 输出，没有颜色也没有光照 —— 拾取不需要知道东西长什么样。
// 深度用的是 Opaque 阶段已经写好的那张，测试用 LessOrEqual 且不写入，
// 所以只有屏幕上真正可见的那个片元会写下 ID，被挡住的不会。

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
