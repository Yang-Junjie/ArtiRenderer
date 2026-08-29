// 漫反射辐照度：对环境 cube 做余弦加权的半球卷积。结果很低频，所以 32² 就够。
//
// **约定警告**：最后乘的是 pi / sample_count，也就是产物是真正的辐照度 E，1/π 那一项**没有**
// 折进来。配套的着色端（forward_pbr.slang）因此是 `base_color * irradiance`，同样不除 π。
// 这一对比严格的 Lambertian（albedo/π · E）亮 π 倍 —— 是从 ArtiRenderer-old 整体搬过来的
// 约定，不是笔误。要调就两边一起调，只改一边会让漫反射差 π 倍（约 3.14，非常明显）。
#include "ibl_common.slang"

[[vk::binding(0, 0)]] TextureCube<float4> environment_map;
[[vk::binding(1, 0)]] SamplerState environment_sampler;
[[vk::binding(2, 0)]] RWTexture2DArray<float4> irradiance_output;

struct PushConstants {
    uint cube_size;
    uint sample_count;
    uint env_size;
    uint env_mip_count;
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
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);

    float3 irradiance = 0.0;
    const float texel_solid_angle =
            4.0 * pi / (6.0 * float(push_constants.env_size) * float(push_constants.env_size));
    for (uint sample_index = 0; sample_index < push_constants.sample_count; ++sample_index) {
        const float2 xi = hammersley(sample_index, push_constants.sample_count);
        const float phi = 2.0 * pi * xi.x;
        const float cos_theta = sqrt(1.0 - xi.y);
        const float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
        const float3 local = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
        const float3 light = normalize(
            tangent * local.x + bitangent * local.y + normal * local.z);
        // 余弦加权采样的 pdf 是 cosTheta / pi，所以每个样本覆盖 pi / (N * cosTheta) 的立体角。
        // 按这个覆盖面积挑对应的 mip 去采 —— 不这么做的话，HDR 里的高频（比如太阳那个小亮斑）
        // 会在样本数不够时表现成一片斑点噪声。
        const float sample_solid_angle =
                pi / (float(push_constants.sample_count) * max(cos_theta, 1.0e-4));
        const float mip_level = clamp(
                0.5 * log2(sample_solid_angle / texel_solid_angle), 0.0,
                float(max(push_constants.env_mip_count, 1) - 1));
        irradiance += environment_map.SampleLevel(environment_sampler, light, mip_level).rgb;
    }
    irradiance *= pi / float(push_constants.sample_count);

    irradiance_output[uint3(thread_id.xy, thread_id.z)] = float4(irradiance, 1.0);
}
