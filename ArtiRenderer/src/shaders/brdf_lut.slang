// split-sum 近似的后一半：环境 BRDF 的积分，只依赖 (NoV, roughness)，所以能预计算成一张
// 2D 查找表。和环境贴图无关 —— 全局烘一次，换 HDR 不用重烘。
//
// 输出 rg 两个通道：着色时用 F * rg.x + rg.y 得到镜面的缩放和偏移。
#include "ibl_common.slang"

[[vk::binding(0, 0)]] RWTexture2D<float4> brdf_output;

struct PushConstants {
    uint lut_size;
    uint sample_count;
    uint pad0;
    uint pad1;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> push_constants;

// 注意 k = a/2 而不是直接光那个 (r+1)²/8：IBL 的 Smith 项用的是另一个取值。
// 混用会让粗糙金属明显偏暗或偏亮，这是两条路必须各带一份的原因。
float geometrySmithApprox(float ndot_v, float ndot_l, float roughness)
{
    const float a = roughness * roughness;
    const float k = a / 2.0;
    const float g1 = ndot_v / (ndot_v * (1.0 - k) + k);
    const float g2 = ndot_l / (ndot_l * (1.0 - k) + k);
    return g1 * g2;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= push_constants.lut_size || thread_id.y >= push_constants.lut_size) {
        return;
    }
    const float2 uv = (float2(thread_id.xy) + 0.5) / float(push_constants.lut_size);
    const float ndot_v = uv.x;
    const float roughness = uv.y;
    // 法线固定 +Z，视线放在 XZ 平面上 —— 各向同性 BRDF 只关心夹角，所以这么摆不失一般性。
    const float3 view = float3(sqrt(1.0 - ndot_v * ndot_v), 0.0, ndot_v);
    const float3 normal = float3(0.0, 0.0, 1.0);

    float a = 0.0;
    float b = 0.0;
    for (uint sample_index = 0; sample_index < push_constants.sample_count; ++sample_index) {
        const float2 xi = hammersley(sample_index, push_constants.sample_count);
        const float3 half_vector = importanceSampleGGX(xi, normal, roughness);
        const float3 light = normalize(2.0 * dot(view, half_vector) * half_vector - view);

        const float ndot_l = max(light.z, 0.0);
        const float ndot_h = max(half_vector.z, 0.0);
        const float vdot_h = max(dot(view, half_vector), 0.0);
        if (ndot_l > 0.0) {
            const float geometry = geometrySmithApprox(ndot_v, ndot_l, roughness);
            const float geometry_visibility =
                geometry * vdot_h / max(ndot_h * ndot_v, 1e-5);
            // 把 Fresnel 拆成 (1-F0)·x + F0·y 两项，F0 因此可以留到着色时再代入。
            const float fresnel = pow(1.0 - vdot_h, 5.0);
            a += (1.0 - fresnel) * geometry_visibility;
            b += fresnel * geometry_visibility;
        }
    }
    a /= float(push_constants.sample_count);
    b /= float(push_constants.sample_count);

    brdf_output[thread_id.xy] = float4(a, b, 0.0, 1.0);
}
