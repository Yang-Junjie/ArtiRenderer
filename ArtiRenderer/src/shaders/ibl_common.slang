// IBL 烘焙用的公共数学。equirect_to_cube / irradiance / prefilter / brdf_lut 都 include 它。

static const float pi = 3.14159265;

// Van der Corput 序列：把整数的二进制位翻转当小数。配上 index/count 就是 Hammersley 低差异
// 序列 —— 比伪随机数收敛快得多，而且完全确定，所以同一张 HDR 每次烘出来逐位一致。
float radicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint index, uint count)
{
    return float2(float(index) / float(count), radicalInverseVdC(index));
}

// 按 GGX 的法线分布重要性采样出一个半程向量。roughness 越大分布越宽。
float3 importanceSampleGGX(float2 xi, float3 normal, float roughness)
{
    const float a = roughness * roughness;
    const float phi = 2.0 * pi * xi.x;
    const float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    const float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    const float3 half_vector = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    // 现造一组切线基。normal 接近 ±Z 时换一个参考轴，否则 cross 会退化。
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * half_vector.x + bitangent * half_vector.y +
                     normal * half_vector.z);
}

// cube 的第 face 个面上、uv ∈ [-1,1] 处对应的方向。面序按 D3D/Vulkan 的
// +X -X +Y -Y +Z -Z，和 nvrhi 的数组切片顺序一致。
float3 directionFromFace(uint face, float2 uv)
{
    if (face == 0) return normalize(float3(1.0, -uv.y, -uv.x));
    if (face == 1) return normalize(float3(-1.0, -uv.y, uv.x));
    if (face == 2) return normalize(float3(uv.x, 1.0, uv.y));
    if (face == 3) return normalize(float3(uv.x, -1.0, -uv.y));
    if (face == 4) return normalize(float3(uv.x, -uv.y, 1.0));
    return normalize(float3(-uv.x, -uv.y, -1.0));
}

// GGX / Trowbridge-Reitz。和 forward_pbr.slang 里那份是同一个公式 —— 这里是采样 pdf 要用。
float distributionGGX(float ndot_h, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denominator = ndot_h * ndot_h * (a2 - 1.0) + 1.0;
    return a2 / max(pi * denominator * denominator, 1.0e-6);
}
