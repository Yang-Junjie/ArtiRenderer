// 镜面反射的预滤波：每个 mip 对应一个 roughness，mip 0 是完全光滑（直接抄环境图）。
// split-sum 近似的前一半 —— 后一半是 brdf_lut.slang。
#include "ibl_common.slang"

[[vk::binding(0, 0)]] TextureCube<float4> environment_map;
[[vk::binding(1, 0)]] SamplerState environment_sampler;
[[vk::binding(2, 0)]] RWTexture2DArray<float4> prefilter_output;

struct PushConstants {
    uint cube_size;
    uint mip_level;
    uint mip_count;
    uint sample_count;
    uint env_size;
    uint env_mip_count;
    uint pad0;
    uint pad1;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= push_constants.cube_size ||
            thread_id.y >= push_constants.cube_size || thread_id.z >= 6) {
        return;
    }
    const float2 uv = (float2(thread_id.xy) + 0.5) / float(push_constants.cube_size);
    const float3 normal = directionFromFace(thread_id.z, uv * 2.0 - 1.0);
    const float roughness =
            float(push_constants.mip_level) / float(max(push_constants.mip_count - 1, 1));
    // 预滤波的经典近似：假设视线方向就是法线方向。这让结果只依赖 (normal, roughness) 两个量，
    // 于是能存成一张 cube 的一个 mip；代价是掠射角的拉长高光丢了。
    const float3 view = normal;

    if (roughness < 0.001) {
        prefilter_output[uint3(thread_id.xy, thread_id.z)] = float4(
                environment_map.SampleLevel(environment_sampler, normal, 0.0).rgb, 1.0);
        return;
    }

    float3 prefiltered = 0.0;
    float total_weight = 0.0;
    const float texel_solid_angle =
            4.0 * pi / (6.0 * float(push_constants.env_size) * float(push_constants.env_size));
    for (uint sample_index = 0; sample_index < push_constants.sample_count; ++sample_index) {
        const float2 xi = hammersley(sample_index, push_constants.sample_count);
        const float3 half_vector = importanceSampleGGX(xi, normal, roughness);
        const float3 light = normalize(2.0 * dot(view, half_vector) * half_vector - view);
        const float ndot_l = max(dot(normal, light), 0.0);
        if (ndot_l > 0.0) {
            const float ndot_h = max(dot(normal, half_vector), 0.0);
            const float vdot_h = max(dot(view, half_vector), 0.0);
            // 和 irradiance 那边同样的道理：按样本覆盖的立体角选 mip，压掉高频噪声。
            const float pdf = distributionGGX(ndot_h, roughness) * ndot_h /
                    max(4.0 * vdot_h, 1.0e-5) + 1.0e-4;
            const float sample_solid_angle =
                    1.0 / (float(push_constants.sample_count) * pdf + 1.0e-5);
            const float mip_level = clamp(
                    0.5 * log2(sample_solid_angle / texel_solid_angle), 0.0,
                    float(max(push_constants.env_mip_count, 1) - 1));
            prefiltered += environment_map.SampleLevel(environment_sampler, light, mip_level)
                    .rgb * ndot_l;
            total_weight += ndot_l;
        }
    }
    prefiltered = total_weight > 0.0 ? prefiltered / total_weight : 0.0;

    prefilter_output[uint3(thread_id.xy, thread_id.z)] = float4(prefiltered, 1.0);
}
