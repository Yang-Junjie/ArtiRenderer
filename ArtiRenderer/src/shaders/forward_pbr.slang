// 前向 PBR 着色（metallic-roughness）。
//
// 顶点属性的 location 必须和 detail::meshVertexLayout() 的顺序一致
// （nvrhi Vulkan 后端按数组下标分配 location）：
//   0 position  1 normal  2 tangent  3 bitangent  4 uv

// 逐 draw。model 之外还传一个法线矩阵：TransformComponent 允许非等比缩放（编辑器里就能拖出来），
// 那种情况下用 model 的左上 3x3 变换法线是错的，必须用 inverse-transpose。
// 64 + 48 = 112 字节，留在 Vulkan 保证的 128 以内。材质参数因此放不进 push constant，
// 改走逐材质的 uniform buffer —— 反正它本来就是按材质而不是按 draw 变的。
struct DrawConstants
{
    float4x4 model;
    // inverse-transpose(model) 的左上 3x3，按行存。w 分量不用，只为对齐到 float4。
    float4 normal_row0;
    float4 normal_row1;
    float4 normal_row2;
};

// 逐帧。全部 float4 / float4x4，HLSL 与 std140 的打包规则因此一致，两侧不用对 padding 猜谜。
struct FrameConstants
{
    float4x4 view_projection;
    float4 camera_position;
    // xyz = 从表面指向光源的单位向量。LightDesc::direction 存的是传播方向，CPU 侧已取反。
    float4 light_direction;
    // xyz = 颜色，w = 强度
    float4 light_color;
    float4 ambient_color;
};

// 逐材质。纹理是采出来的原值，这里是乘在上面的系数。
struct MaterialConstants
{
    float4 base_color;
    // x = metallic, y = roughness, z = occlusion 强度, w = emissive 强度
    float4 factors;
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw_constants;
// 绑定号从 1 开始：push constant 在 nvrhi 里固定占 b0（BindingLayoutItem::PushConstants(0, ...)），
// 再有 constant buffer 落在 binding 0 就会撞成「duplicate bindings: b0」。
// 而且同一个 set 里所有资源的 slot 号必须互不相同（反射层按 slot 去重，不分资源类型）。
[[vk::binding(1, 0)]] ConstantBuffer<FrameConstants> frame_constants;
[[vk::binding(2, 0)]] ConstantBuffer<MaterialConstants> material_constants;
[[vk::binding(3, 0)]] Texture2D<float4> base_color_texture;
[[vk::binding(4, 0)]] Texture2D<float4> metallic_roughness_texture;
[[vk::binding(5, 0)]] Texture2D<float4> normal_texture;
[[vk::binding(6, 0)]] Texture2D<float4> occlusion_texture;
[[vk::binding(7, 0)]] Texture2D<float4> emissive_texture;
// 五张贴图共用一个采样器。TextureDesc 目前不带采样状态，所以没有逐贴图的过滤/寻址可言；
// 真要按 glTF 的 sampler 走时，改的是 TextureDesc 而不是这里。
[[vk::binding(8, 0)]] SamplerState material_sampler;

static const float kPi = 3.14159265359;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float3 tangent : TANGENT;
    [[vk::location(3)]] float3 bitangent : BINORMAL;
    [[vk::location(4)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    float3 world_tangent : TEXCOORD3;
    float3 world_bitangent : TEXCOORD4;
    float2 uv : TEXCOORD0;
};

float3 transformNormal(float3 normal)
{
    return float3(dot(draw_constants.normal_row0.xyz, normal),
            dot(draw_constants.normal_row1.xyz, normal),
            dot(draw_constants.normal_row2.xyz, normal));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 world = mul(draw_constants.model, float4(input.position, 1.0));

    VertexOutput output;
    output.position = mul(frame_constants.view_projection, world);
    output.world_position = world.xyz;
    output.world_normal = transformNormal(input.normal);
    // 切线和副切线是**方向**而不是法线，所以走 model 而不是法线矩阵。两个都用同一个矩阵变换，
    // 镜像变换（行列式为负）下手性会一致地翻过来，不需要额外的符号位。
    output.world_tangent = mul((float3x3)draw_constants.model, input.tangent);
    output.world_bitangent = mul((float3x3)draw_constants.model, input.bitangent);
    output.uv = input.uv;
    return output;
}

// GGX / Trowbridge-Reitz 法线分布。alpha = roughness²是 Disney 那套「感知线性」的约定。
float distributionGGX(float noh, float roughness)
{
    const float alpha = roughness * roughness;
    const float alpha_squared = alpha * alpha;
    const float denominator = noh * noh * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(kPi * denominator * denominator, 1e-6);
}

// Smith 几何项，Schlick-GGX 近似。k 用直接光的那个取值 (r+1)²/8；IBL 用的是 r²/2，
// 两者不能混，所以等加 IBL 时那条路要自己带一份。
float geometrySmith(float nov, float nol, float roughness)
{
    const float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    const float ggx_v = nov / lerp(nov, 1.0, k);
    const float ggx_l = nol / lerp(nol, 1.0, k);
    return ggx_v * ggx_l;
}

float3 fresnelSchlick(float3 f0, float voh)
{
    return f0 + (1.0 - f0) * pow(1.0 - voh, 5.0);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 base_color_sample =
            base_color_texture.Sample(material_sampler, input.uv) * material_constants.base_color;
    // glTF 的约定：G 通道是 roughness，B 通道是 metallic。贴图缺失时兜底的白图给出 1.0，
    // 再乘系数，所以「没有贴图」等价于「只有系数」。
    const float2 metallic_roughness =
            metallic_roughness_texture.Sample(material_sampler, input.uv).bg;

    const float3 base_color = max(base_color_sample.rgb, 0.0);
    const float metallic = saturate(metallic_roughness.x * material_constants.factors.x);
    // 下限 0.045：再低下去 GGX 的高光会退化成一个亮度爆掉的点。
    const float roughness = clamp(metallic_roughness.y * material_constants.factors.y, 0.045, 1.0);
    const float occlusion = lerp(1.0,
            occlusion_texture.Sample(material_sampler, input.uv).r,
            saturate(material_constants.factors.z));
    const float3 emissive = emissive_texture.Sample(material_sampler, input.uv).rgb *
            material_constants.factors.w;

    // 切线空间法线。贴图缺失时兜底的是内建 flat normal 图（128,128,255），解出来正好是 (0,0,1)，
    // 也就是「就用几何法线」—— 所以这里不需要分支。
    const float3 geometric_normal = normalize(input.world_normal);
    // Gram-Schmidt 正交化：插值之后 TBN 不再正交。
    const float3 tangent =
            normalize(input.world_tangent - geometric_normal * dot(geometric_normal, input.world_tangent));
    const float3 bitangent = normalize(input.world_bitangent -
            geometric_normal * dot(geometric_normal, input.world_bitangent) -
            tangent * dot(tangent, input.world_bitangent));
    const float3 tangent_normal =
            normal_texture.Sample(material_sampler, input.uv).xyz * 2.0 - 1.0;
    const float3 normal =
            normalize(tangent_normal.x * tangent + tangent_normal.y * bitangent +
                    tangent_normal.z * geometric_normal);

    const float3 view_direction = normalize(frame_constants.camera_position.xyz - input.world_position);
    const float3 light_direction = normalize(frame_constants.light_direction.xyz);
    // nov 不能为 0：它出现在 specular 的分母里。
    const float nov = max(dot(normal, view_direction), 1e-4);
    const float nol = max(dot(normal, light_direction), 0.0);
    // 介质的垂直反射率固定 0.04，金属用自己的 base_color 当 F0 —— metallic 工作流的全部内容。
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_color, metallic);

    float3 direct = float3(0.0, 0.0, 0.0);
    if (nol > 0.0)
    {
        const float3 half_direction = normalize(view_direction + light_direction);
        const float noh = max(dot(normal, half_direction), 0.0);
        const float voh = max(dot(view_direction, half_direction), 0.0);

        const float3 fresnel = fresnelSchlick(f0, voh);
        const float3 specular = distributionGGX(noh, roughness) *
                geometrySmith(nov, nol, roughness) * fresnel / max(4.0 * nov * nol, 1e-4);
        // 金属没有漫反射；(1 - fresnel) 是能量守恒 —— 反射掉的那部分不再进入次表面。
        const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color / kPi;

        const float3 radiance = frame_constants.light_color.rgb * frame_constants.light_color.w;
        direct = (diffuse + specular) * radiance * nol;
    }

    // 环境项是 IBL 落地之前的占位：一个常数环境光。没有方向可言，所以按 metallic 硬分成
    // 漫反射和镜面两份，而不是走 Fresnel。给金属留一份镜面是刻意的 —— 否则金属在没有 IBL 时
    // 会是纯黑，看起来像 BRDF 写错了。IBL 会把这两行换成 irradiance / prefiltered + BRDF LUT。
    const float3 ambient = frame_constants.ambient_color.rgb * occlusion;
    const float3 ambient_diffuse = ambient * base_color * (1.0 - metallic);
    const float3 ambient_specular = ambient * f0;

    return float4(direct + ambient_diffuse + ambient_specular + emissive, base_color_sample.a);
}
