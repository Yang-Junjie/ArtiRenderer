// 前向 Blinn-Phong 着色。
//
// 顶点属性的 location 必须和 detail::meshVertexLayout() 的顺序一致
// （nvrhi Vulkan 后端按数组下标分配 location）：
//   0 position  1 normal  2 tangent  3 bitangent  4 uv

// 逐 draw。只放 model 而不是 MVP：view_projection 是逐帧的，放进 UBO，MVP 在顶点着色器里算。
// 这样 push constant 只用 96 字节，留在 Vulkan 保证的 128 以内 —— 同时传 MVP 和 model 就会超。
struct DrawConstants
{
    float4x4 model;
    float4 base_color;
    // xyz = specular color 已乘上 specular_strength，w = shininess
    float4 specular;
};

// 逐帧。全部用 float4 / float4x4，这样 HLSL 与 std140 的打包规则一致，C++ 侧不用猜 padding。
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

[[vk::push_constant]] ConstantBuffer<DrawConstants> draw_constants;
[[vk::binding(0, 0)]] Texture2D<float4> base_color_texture;
[[vk::binding(1, 0)]] SamplerState base_color_sampler;
[[vk::binding(2, 0)]] ConstantBuffer<FrameConstants> frame_constants;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(4)]] float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    const float4 world = mul(draw_constants.model, float4(input.position, 1.0));

    VertexOutput output;
    output.position = mul(frame_constants.view_projection, world);
    output.world_position = world.xyz;
    // 用 model 的左上 3x3 变换法线。对刚体变换（旋转、平移、等比缩放）是正确的；非等比缩放
    // 需要 inverse-transpose，届时要另外传一个 normal matrix 进来。
    output.world_normal = mul((float3x3)draw_constants.model, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    const float4 albedo =
            base_color_texture.Sample(base_color_sampler, input.uv) * draw_constants.base_color;

    const float3 normal = normalize(input.world_normal);
    const float3 to_light = normalize(frame_constants.light_direction.xyz);
    const float3 to_view = normalize(frame_constants.camera_position.xyz - input.world_position);

    const float3 radiance = frame_constants.light_color.rgb * frame_constants.light_color.w;
    const float diffuse_term = max(dot(normal, to_light), 0.0);

    // Blinn-Phong 与 Phong 的区别就在这里：用半程向量和法线的夹角，而不是反射向量和视线的夹角。
    const float3 half_vector = normalize(to_light + to_view);
    const float specular_angle = max(dot(normal, half_vector), 0.0);
    // 背光面不该有高光。diffuse 为 0 时把镜面项一起压掉，否则掠射角会出现一圈亮边。
    const float specular_term = diffuse_term > 0.0
            ? pow(specular_angle, max(draw_constants.specular.w, 1.0))
            : 0.0;

    const float3 ambient = frame_constants.ambient_color.rgb * albedo.rgb;
    const float3 diffuse = radiance * albedo.rgb * diffuse_term;
    const float3 specular = radiance * draw_constants.specular.rgb * specular_term;

    return float4(ambient + diffuse + specular, albedo.a);
}
