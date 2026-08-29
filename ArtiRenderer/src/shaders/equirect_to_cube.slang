// 等距柱状投影（equirectangular）的 HDR → cubemap 的 mip 0。
//
// cube 按 RWTexture2DArray 写：cubemap 的 UAV 视图必须当 2D array 看，六个面就是六个数组切片。
#include "ibl_common.slang"

[[vk::binding(0, 0)]] Texture2D<float4> equirect_texture;
[[vk::binding(1, 0)]] SamplerState equirect_sampler;
[[vk::binding(2, 0)]] RWTexture2DArray<float4> cube_output;

struct PushConstants {
    uint cube_size;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

// 方向 → 等距柱状 UV。u 是绕 Y 轴的方位角，v 是与 +Y 的夹角。
// 采样器在 U 方向是 repeat，所以接缝处（atan2 的 ±π）能正确插值。
float2 directionToEquirectUv(float3 direction)
{
    const float u = atan2(direction.z, direction.x) / (2.0 * pi) + 0.5;
    const float v = acos(clamp(direction.y, -1.0, 1.0)) / pi;
    return float2(u, v);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= push_constants.cube_size ||
            thread_id.y >= push_constants.cube_size || thread_id.z >= 6) {
        return;
    }
    const float2 uv = (float2(thread_id.xy) + 0.5) / float(push_constants.cube_size);
    const float3 direction = directionFromFace(thread_id.z, uv * 2.0 - 1.0);
    const float4 color = equirect_texture.SampleLevel(
        equirect_sampler, directionToEquirectUv(direction), 0.0);
    cube_output[uint3(thread_id.xy, thread_id.z)] = color;
}
