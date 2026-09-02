// 级联阴影的深度写入。把几何按某一级 cascade 的光源正交视锥重画一遍，只写深度。
//
// 没有颜色输出：framebuffer 只挂了一个深度附件（ShadowTargets 里那张 Texture2DArray 的一个
// slice），零个颜色附件。fragmentMain 因此是空的 —— 但**必须存在**：
// SlangCompiler::compileGraphics 永远要 vertex + fragment 两个 entry point，没有 vertex-only
// 路径。写个空函数比去改编译器便宜得多。

// 必须和 C++ 侧的 kShadowCascadeCount 一致（shadow_targets.h）。Slang 要编译期常量，
// 所以这里是个字面量 —— 对不上不会有任何提示，改一边必须改另一边。
static const uint kCascadeCount = 4;

// 每一级的光源 view-projection。整个数组一次传进来、用 push constant 里的下标选，
// 而不是每级重写一次 UBO：后者要在 pass 中间改一个已经绑上的 buffer，得插屏障。
struct ShadowCascadeData
{
    float4x4 light_view_projection[kCascadeCount];
};

// binding **1**而不是 0：push constant 块在反射里占的就是 b0，再拿一个 cbuffer 放 0 会得到
// 「Binding layout contains duplicate bindings: b0」。gbuffer.slang 也是因为有 push constant
// 而从 binding(1, 0) 开始排的 —— 纹理不受影响（imgui.slang 的 binding(0, 0) 是张纹理），
// 只有 cbuffer 会和 push constant 撞。
[[vk::binding(1, 0)]] ConstantBuffer<ShadowCascadeData> shadow_cascades;

struct ShadowConstants
{
    float4x4 model;
    uint cascade;
    // **这里不要补 padding。** Slang 会把整块的大小向上取整到最大成员的对齐（float4x4 → 16），
    // 所以 64 + 4 自然就是 80 字节；补齐是 **C++ 侧**的事（让 sizeof 对上 80）。
    //
    // 在 shader 里补 uint3 反而会把块撑到 96：cbuffer 布局规则下 uint3 不能跨 16 字节边界，
    // 从 68 起会被推到 80，于是 80 + 12 → 96。踩过一次，报错是
    // 「Push constant size (80 bytes) doesn't match the size expected by the pipeline (96 bytes)」。
};

[[vk::push_constant]] ConstantBuffer<ShadowConstants> constants;

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
    // 分两步乘（先 model 再 light VP），和 gbuffer.slang 一个写法。这里不需要和别的 pass
    // 逐位一致：阴影图的深度只和它自己比。
    const float4 world_position = mul(constants.model, float4(input.position, 1.0));

    VertexOutput output;
    output.position = mul(shadow_cascades.light_view_projection[constants.cascade], world_position);
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
    // 只写深度，什么都不输出。深度由固定功能光栅化器写进附件。
}
