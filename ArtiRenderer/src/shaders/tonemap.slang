// 把场景线性 HDR 的 SceneColor 压成显示线性 LDR 的 DisplayColor。全屏三角形，不需要顶点缓冲。
//
// 只做曝光 + 曲线，不做 sRGB 编码：编码由 backbuffer 的 sRGB view 在写入时完成（PresentPass），
// 编辑器模式下由 ImGui 采样 DisplayColor 再写 backbuffer，同样走那条硬件编码。这一层在两条路上
// 的含义因此是一致的 —— 「已经压进 [0,1] 的线性值」。

struct TonemapConstants
{
    // 线性曝光倍率，进曲线之前乘上去。
    float exposure;
};

[[vk::push_constant]] ConstantBuffer<TonemapConstants> tonemap_constants;
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

// ACES 的拟合曲线（Narkowicz 2015）。选它而不是 Reinhard 是为了高光的滚降：Reinhard 把
// 高亮区压得太平，金属高光和天光会失去层次；选它而不是完整的 ACES RRT/ODT 是因为那需要
// 3x3 色域矩阵和一张 LUT，收益在这个阶段还看不出来。
//
// 曲线自带约 0.8 的中灰增益，所以同一个场景换上 tone mapping 之后会比原来的直通稍亮 ——
// 这是预期的，不是曝光算错了。
float3 acesFilmic(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

[shader("fragment")]
float4 fragmentMain(FullscreenVertexOutput input) : SV_Target
{
    const float3 scene = scene_color.Sample(scene_sampler, input.uv).rgb;
    // 负值会让曲线的分母过零，采样器在边缘做插值时也可能引入极小的负数，所以先夹一下。
    const float3 exposed = max(scene * tonemap_constants.exposure, 0.0);
    return float4(acesFilmic(exposed), 1.0);
}
