// G-Buffer 写入：把「被光照的表面」编码成 metallic-roughness 的几何 + 材质属性。
// 光照本身不在这里发生 —— 那是 deferred_lighting.slang 的事。
//
// 只服务 MaterialType::PBR —— 延迟管线里这是唯一被求值的着色模型。要再加几何 pass 时，
// 划分依据是**另一套 G-Buffer 编码**（比如透明、蒙皮），不是材质类型：类型一样、编码一样，
// 分成两个 pass 就只是两份一样的 PSO。
//
// 顶点属性的 location 必须和 detail::meshVertexLayout() 的顺序一致
// （nvrhi Vulkan 后端按数组下标分配 location）：
//   0 position  1 normal  2 tangent  3 bitangent  4 uv

// ===== G-Buffer 编码契约（deferred_lighting.slang 必须和这里逐通道对齐）=====
//
//   Target0  SRGBA8_UNORM   rgb = albedo（写入线性值，sRGB 编码由硬件完成）
//                           a   = metallic
//   Target1  RGBA16_FLOAT   xyz = 世界法线（不保证归一化，光照端 normalize）
//                           w   = roughness
//   Target2  RGBA16_FLOAT   rgb = emissive（场景线性 HDR，可以远超 1.0）
//                           a   = occlusion
//   深度     D32            复用 SceneDepth，光照端采它反投影出世界坐标
//
// albedo 用 SRGBA8 而不是线性 RGBA8：albedo 是个 [0,1] 的反射率，8 位线性在暗部会有可见带状，
// 借硬件的 sRGB 编码等于免费拿到感知均匀的精度分布。alpha 通道不受 sRGB 影响（Vulkan 规定
// 只编码 RGB），所以 metallic 存在里面是线性的。

struct DrawConstants
{
    float4x4 model;
    // inverse-transpose(model) 的左上 3x3，按行存。w 分量不用，只为对齐到 float4。
    // 传法线矩阵而不是让着色器用 model 的左上 3x3：TransformComponent 允许非等比缩放，
    // 那种情况下后者是错的。64 + 48 = 112 字节，留在 Vulkan 保证的 128 以内。
    float4 normal_row0;
    float4 normal_row1;
    float4 normal_row2;
};

// 逐帧。延迟管线下这里只剩相机矩阵 —— 光源、环境光、IBL 全部搬去了光照 pass，
// 几何 pass 因此完全不认识光照。
struct FrameConstants
{
    float4x4 view_projection;
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
    float3 world_normal : TEXCOORD1;
    float3 world_tangent : TEXCOORD2;
    float3 world_bitangent : TEXCOORD3;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput
{
    float4 albedo_metallic : SV_Target0;
    float4 normal_roughness : SV_Target1;
    float4 emissive_occlusion : SV_Target2;
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
    output.world_normal = transformNormal(input.normal);
    // 切线和副切线是**方向**而不是法线，所以走 model 而不是法线矩阵。两个都用同一个矩阵变换，
    // 镜像变换（行列式为负）下手性会一致地翻过来，不需要额外的符号位。
    output.world_tangent = mul((float3x3)draw_constants.model, input.tangent);
    output.world_bitangent = mul((float3x3)draw_constants.model, input.bitangent);
    output.uv = input.uv;
    // world_position 不再往下传：光照端从深度反投影出来，插值一份反而是两个真值来源。
    return output;
}

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input)
{
    const float4 base_color_sample =
            base_color_texture.Sample(material_sampler, input.uv) * material_constants.base_color;
    // glTF 的约定：G 通道是 roughness，B 通道是 metallic。贴图缺失时兜底的白图给出 1.0，
    // 再乘系数，所以「没有贴图」等价于「只有系数」。
    const float2 metallic_roughness =
            metallic_roughness_texture.Sample(material_sampler, input.uv).bg;

    const float3 albedo = max(base_color_sample.rgb, 0.0);
    const float metallic = saturate(metallic_roughness.x * material_constants.factors.x);
    // 下限 0.045：再低下去 GGX 的高光会退化成一个亮度爆掉的点。夹在写入侧而不是光照侧，
    // 这样 G-Buffer 里躺着的就已经是可用的值，调试视图看到的和着色用的是同一个数。
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
    const float3 tangent = normalize(
            input.world_tangent - geometric_normal * dot(geometric_normal, input.world_tangent));
    const float3 bitangent = normalize(input.world_bitangent -
            geometric_normal * dot(geometric_normal, input.world_bitangent) -
            tangent * dot(tangent, input.world_bitangent));
    const float3 tangent_normal =
            normal_texture.Sample(material_sampler, input.uv).xyz * 2.0 - 1.0;
    const float3 normal = normalize(tangent_normal.x * tangent + tangent_normal.y * bitangent +
            tangent_normal.z * geometric_normal);

    FragmentOutput output;
    output.albedo_metallic = float4(albedo, metallic);
    output.normal_roughness = float4(normal, roughness);
    output.emissive_occlusion = float4(emissive, occlusion);
    // base_color 的 alpha 到此为止：这条路径上的材质都是不透明的，没有第二个消费者。
    // 真做混合透明时它要走的是另一条 pass，不是 G-Buffer。
    return output;
}
