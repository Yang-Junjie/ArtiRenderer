#include "deferred_lighting_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "log.h"
#include "shader_paths.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace arti::rendering {
namespace {

// 全部走 UBO 而不是 push constant：64 + 4x16 + 16 = 144 字节，远超 Vulkan 保证的 128。
// 成员全是 float4 / float4x4 / uint4，三者在 cbuffer 里都是 16 字节对齐，
// HLSL 与 std140 的打包规则因此一致，两侧不用对 padding 猜谜。
struct LightingConstants {
    std::array<float, 16> inverse_view_projection;
    std::array<float, 4> camera_position;
    std::array<float, 4> ambient_color;
    // x = 环境强度，y = IBL 是否就绪，z = prefiltered mip 数，w 未用
    std::array<float, 4> environment_params;
    // RenderScene::clear_color。没有几何体覆盖的像素直接输出它 —— 这个 pass 是 SceneColor
    // 每个像素的唯一写入者，所以背景色也得由它来写。
    std::array<float, 4> background_color;
    // x = 本帧的光源数量，yzw 未用。
    std::array<uint32_t, 4> light_count;
};

static_assert(std::is_standard_layout_v<LightingConstants>);
static_assert(sizeof(LightingConstants) == 144);

// GPU 侧的一个光源。和 deferred_lighting.slang 里的 GpuLight 逐字段对齐 —— 改一边必须改另一边。
struct GpuLight {
    std::array<float, 4> position_range;
    std::array<float, 4> direction_type;
    std::array<float, 4> color_intensity;
    std::array<float, 4> cone_scale_offset;
};

static_assert(std::is_standard_layout_v<GpuLight>);
static_assert(sizeof(GpuLight) == sizeof(float) * 16);

// 光源缓冲的起始容量。8 个灯是「小场景不用重建缓冲」和「别为空场景白占显存」之间的折中，
// 超了就翻倍，不设上限。
constexpr size_t kInitialLightCapacity = 8;

// 类型枚举到 GPU 侧的编号。**必须和 deferred_lighting.slang 里的 kLightDirectional /
// kLightPoint / kLightSpot 一致** —— 那边是 static const uint，编译期常量，对不上不会有任何提示。
uint32_t lightTypeIndex(LightType type) noexcept {
    switch (type) {
        case LightType::Directional:
            return 0;
        case LightType::Point:
            return 1;
        case LightType::Spot:
            return 2;
    }
    return 0;
}

GpuLight toGpuLight(const LightDesc& light) {
    GpuLight gpu{};

    // range 夹一个下限：0 会让距离衰减的窗口整个塌掉，那样这个灯什么都照不亮，
    // 看起来像是灯坏了而不是参数填错了。
    const glm::vec4 position{ light.position, std::max(light.range, 1e-3f) };
    std::memcpy(gpu.position_range.data(), glm::value_ptr(position), sizeof(position));

    // 存的是**传播方向**的原值，不在这里取反 —— 着色端谁需要「从表面指向光源」谁自己取反。
    // 聚光的锥轴要的正是传播方向，两种含义挤进一个字段迟早出错。归一化在这里做一次，
    // 省得每个像素都做。
    const float length = glm::length(light.direction);
    const glm::vec3 forward =
            length > 0.0f ? light.direction / length : glm::vec3{ 0.0f, -1.0f, 0.0f };
    const glm::vec4 direction{ forward, static_cast<float>(lightTypeIndex(light.type)) };
    std::memcpy(gpu.direction_type.data(), glm::value_ptr(direction), sizeof(direction));

    const glm::vec4 color{ glm::vec3{ light.color }, light.intensity };
    std::memcpy(gpu.color_intensity.data(), glm::value_ptr(color), sizeof(color));

    // 聚光的角度衰减折成一次 mad：saturate(cos * scale + offset)。
    // scale = 1/(cosInner - cosOuter)、offset = -cosOuter * scale，于是 cos 落在 cosOuter 上是
    // 0、落在 cosInner 上是 1。cos 随角度递减，所以 inner <= outer 时分母恒正 ——
    // 先把 inner 夹到 outer 以内保证这一点，再给分母垫一个下限：inner == outer 时它会塌到 0，
    // 那种配置想要的是硬边，不是除零。
    const float outer = std::max(light.outer_cone_radians, 0.0f);
    const float inner = std::clamp(light.inner_cone_radians, 0.0f, outer);
    const float cos_outer = std::cos(outer);
    const float scale = 1.0f / std::max(std::cos(inner) - cos_outer, 1e-4f);
    gpu.cone_scale_offset = { scale, -cos_outer * scale, 0.0f, 0.0f };
    return gpu;
}

// 阴影的常量。单独一个 UBO 而不是塞进 LightingConstants：那个结构已经 144 字节且带
// static_assert，加四个矩阵会让它变成 400+ 字节；而且阴影参数和「这一帧怎么呈现」不是一回事。
struct ShadowConstantsBuffer {
    std::array<std::array<float, 16>, kShadowCascadeCount> light_view_projection{};
    // 每级覆盖到多远（view-space 距离），xyzw 对应四级。
    std::array<float, 4> split_far{};
    // x = 阴影总距离，y = 淡出起点比例，z = 一个 texel 在 UV 里的大小，
    // w = 投影光源在**GPU 光源缓冲**里的下标（不是 RenderScene::lights 的下标，见 record()）。
    std::array<float, 4> params{};
};

static_assert(std::is_standard_layout_v<ShadowConstantsBuffer>);
static_assert(sizeof(ShadowConstantsBuffer) == 64 * kShadowCascadeCount + 32);

// Texture2DArray 的绑定项。NvrhiBindingResource::Texture 默认不填维度，对一张 array
// 纹理必须显式给，否则 nvrhi 会按 2D 建 SRV。
arti::renderer::vulkan::NvrhiBindingResource shadowMapBinding(nvrhi::ITexture& texture) {
    auto resource = arti::renderer::vulkan::NvrhiBindingResource::Texture("shadow_map", texture);
    resource.dimension = nvrhi::TextureDimension::Texture2DArray;
    return resource;
}

// 「这一帧没有阴影」的哨兵。着色端拿它和光源下标比，永远不相等 —— 比多传一个 bool 省一个字段。
constexpr float kNoShadowLight = 1e9f;

// 淡出起点，占总距离的比例。拄 Godot 的 fade_start。阶段 6 才真正用上。
constexpr float kShadowFadeStart = 0.8f;
} // namespace

struct DeferredLightingPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    // G-Buffer 专用采样器：**point + clamp**。G-Buffer 和这个 pass 同分辨率、1:1 对应，
    // 线性过滤在这里没有意义，但会在几何边缘把两侧的法线和材质插值混起来，
    // 混出来的既不是这边的表面也不是那边的。
    nvrhi::SamplerHandle gbuffer_sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    // PSO 只依赖格式和采样数，尺寸变了不用重建。
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle shadow_constants;
    // 阴影图的采样器：**linear + clamp**。linear 是为了 PCF 的九个采样点自带一点
    // 硬件插值（边缘更平）；clamp 是为了越界时不会绕回到另一侧—— 不过越界已经在
    // 着色端先判掉了，这条是兵底。
    nvrhi::SamplerHandle shadow_sampler;

    // 光源缓冲每个 frame slot 一份，照 ImGuiPass / DebugLinePass 的形状：多帧在飞的时候，
    // 下一帧的 writeBuffer 会撞上上一帧还在读的那份（WAR）。自动状态跟踪会插 barrier 保正确，
    // 但那意味着新一帧的拷贝要等旧一帧的片元读完 —— 一个白给的跨帧串行点。
    //
    // 缓冲进了绑定集（是个 StructuredBuffer SRV，不像顶点缓冲那样单独绑），所以绑定集也得
    // 跟着按 slot 分。各 slot 的绑定集不是同一帧建的，失效条件因此也各记一份。
    struct FrameSlot {
        nvrhi::BufferHandle lights;
        size_t light_capacity{ 0 };
        nvrhi::BindingSetHandle binding_set;
        // 这个 slot 的绑定集是按哪一版资源建的：G-Buffer 改尺寸、环境重烘、光源缓冲扩容，
        // 任一发生就要重建。初值取 max 是为了第一次一定不相等。
        uint64_t bound_gbuffer_revision{ std::numeric_limits<uint64_t>::max() };
        uint64_t bound_environment_revision{ std::numeric_limits<uint64_t>::max() };
        uint64_t bound_shadow_revision{ std::numeric_limits<uint64_t>::max() };
        nvrhi::IBuffer* bound_lights{ nullptr };
    };
    std::vector<FrameSlot> frame_slots;

    // 本帧的光源，每帧 clear + 填充。CPU 侧只要一份 —— 它在同一帧里就写进 GPU 缓冲了，
    // 不跨帧存活，所以不用按 slot 分。留成成员只是为了复用容量。
    std::vector<GpuLight> lights;

    // 光源缓冲按需增长，不设上限。容量翻倍而不是精确匹配：帧间光源数抖动一两个不该每帧重建
    // 缓冲，重建会连带重建这个 slot 的绑定集。
    void ensureLightBuffer(nvrhi::IDevice& device, FrameSlot& slot, size_t count) {
        // 至少留一个元素。0 字节的缓冲建不起来，而绑定集需要一个有效的缓冲 ——
        // 没有光源的帧靠 light_count = 0 让着色器一次循环都不进，不靠空缓冲。
        const size_t needed = std::max<size_t>(count, 1);
        if (slot.lights && slot.light_capacity >= needed) {
            return;
        }
        size_t capacity = std::max(slot.light_capacity, kInitialLightCapacity);
        while (capacity < needed) {
            capacity *= 2;
        }
        nvrhi::BufferDesc desc;
        desc.setByteSize(capacity * sizeof(GpuLight))
                .setStructStride(sizeof(GpuLight))
                .setDebugName("ArtiRenderer LightBuffer")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
        slot.lights = device.createBuffer(desc);
        if (!slot.lights) {
            throw std::runtime_error("NVRHI failed to create the deferred lighting light buffer.");
        }
        slot.light_capacity = capacity;
        getLogChannel().debug("Light buffer resized to {} lights", capacity);
    }

    // 绑定集在 **record()** 里建，不在 prepare()：EnvironmentResources 的那几个句柄要到
    // EnvironmentBakePass::record() 才填上（连兜底的 1x1 黑图也是在那里发布的），prepare
    // 阶段读到的还是空句柄，建集会直接抛。EnvironmentBake < Lighting 这个 stage 顺序保证了
    // 到这里它们一定有效。
    void ensureBindingSet(PassRecordContext& context, FrameSlot& slot) {
        auto& device = context.device();
        auto& targets = context.targets();
        auto& gbuffer = context.gbuffer();
        const auto& environment = context.environment();
        auto& shadows = context.shadows();
        if (slot.binding_set && slot.bound_gbuffer_revision == gbuffer.revision() &&
                slot.bound_environment_revision == environment.revision &&
                slot.bound_shadow_revision == shadows.revision() &&
                slot.bound_lights == slot.lights.Get()) {
            return;
        }
        const std::array resources = {
            arti::renderer::vulkan::NvrhiBindingResource::Buffer("lighting_constants", *constants),
            arti::renderer::vulkan::NvrhiBindingResource::Texture("g_albedo_metallic",
                    gbuffer.albedoMetallic()),
            arti::renderer::vulkan::NvrhiBindingResource::Texture("g_normal_roughness",
                    gbuffer.normalRoughness()),
            arti::renderer::vulkan::NvrhiBindingResource::Texture("g_emissive_occlusion",
                    gbuffer.emissiveOcclusion()),
            // 深度当 SRV 采。D32 绑到 Texture2D<float> 上，nvrhi 会为它建深度专用的 SRV；
            // 自动状态跟踪负责从 DepthWrite 转到 ShaderResource 再转回去（PickingPass 还要
            // 把它当深度附件用）。
            arti::renderer::vulkan::NvrhiBindingResource::Texture("g_depth", targets.sceneDepth()),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("gbuffer_sampler",
                    *gbuffer_sampler),
            arti::renderer::vulkan::NvrhiBindingResource::Texture("irradiance_cube",
                    *environment.irradiance),
            arti::renderer::vulkan::NvrhiBindingResource::Texture("prefiltered_cube",
                    *environment.prefiltered),
            arti::renderer::vulkan::NvrhiBindingResource::Texture("brdf_lut",
                    *environment.brdf_lut),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("ibl_sampler",
                    *environment.sampler),
            arti::renderer::vulkan::NvrhiBindingResource::Buffer("lights", *slot.lights),
            arti::renderer::vulkan::NvrhiBindingResource::Buffer("shadow_constants",
                    *shadow_constants),
            // Texture2DArray 得显式告诉绑定层维度：反射看到的是数组类型，但 NvrhiBindingResource
            // 默认按 Unknown 走，维度对不上时建出来的 SRV 是错的。
            shadowMapBinding(shadows.depthArray()),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("shadow_sampler",
                    *shadow_sampler),
        };
        slot.binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device, reflection, 0,
                *binding_layout, resources);
        if (!slot.binding_set) {
            throw std::runtime_error("NVRHI failed to create the deferred lighting binding set.");
        }
        slot.bound_gbuffer_revision = gbuffer.revision();
        slot.bound_environment_revision = environment.revision;
        slot.bound_shadow_revision = shadows.revision();
        slot.bound_lights = slot.lights.Get();
    }
};

DeferredLightingPass::DeferredLightingPass()
        : m_impl(std::make_unique<Impl>()) {}

DeferredLightingPass::~DeferredLightingPass() = default;

void DeferredLightingPass::prepare(PassPrepareContext& context) {
    auto& device = context.device();
    auto& targets = context.targets();

    m_impl->frame_slots.resize(context.frameSlotCount());

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("deferred_lighting.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(device, program,
                "ArtiRenderer deferred lighting");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The deferred lighting shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        // setAllFilters(false) = point。理由见 Impl 里那条注释。
        nvrhi::SamplerDesc sampler_desc;
        sampler_desc.setAllFilters(false).setAllAddressModes(
                nvrhi::SamplerAddressMode::ClampToEdge);
        m_impl->gbuffer_sampler = device.createSampler(sampler_desc);
        if (!m_impl->gbuffer_sampler) {
            throw std::runtime_error("NVRHI failed to create the deferred lighting sampler.");
        }

        // 阴影采样器：linear + clamp。linear 让 PCF 的九个采样点自带一点硬件插值，
        // 边缘比纯 point 平一些。采的是原始深度而不是比较结果，所以插值插的是深度值
        // —— 严格说不对（深度不能线性插），但在 3x3 平均里的影响比它带来的平滑小。
        // 真要严谨得上硬件比较采样器（SamplerComparisonState）。
        nvrhi::SamplerDesc shadow_sampler_desc;
        shadow_sampler_desc.setAllFilters(true).setAllAddressModes(
                nvrhi::SamplerAddressMode::ClampToEdge);
        m_impl->shadow_sampler = device.createSampler(shadow_sampler_desc);
        if (!m_impl->shadow_sampler) {
            throw std::runtime_error("NVRHI failed to create the shadow sampler.");
        }

        // 刻意**不用** volatile：binding layout 是从 shader 反射来的，反射看不到 buffer 是不是
        // volatile，只能发出普通 ConstantBuffer，而 BindingSetItem 会从 buffer 自动判定成
        // VolatileConstantBuffer，两者不一致 Vulkan 会报 descriptor type 错误。
        nvrhi::BufferDesc constants_desc;
        constants_desc.setByteSize(sizeof(LightingConstants))
                .setIsConstantBuffer(true)
                .setDebugName("ArtiRenderer LightingConstants")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
        m_impl->constants = device.createBuffer(constants_desc);
        if (!m_impl->constants) {
            throw std::runtime_error(
                    "NVRHI failed to create the deferred lighting constant buffer.");
        }

        nvrhi::BufferDesc shadow_desc;
        shadow_desc.setByteSize(sizeof(ShadowConstantsBuffer))
                .setIsConstantBuffer(true)
                .setDebugName("ArtiRenderer ShadowConstants")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
        m_impl->shadow_constants = device.createBuffer(shadow_desc);
        if (!m_impl->shadow_constants) {
            throw std::runtime_error("NVRHI failed to create the shadow constant buffer.");
        }
    }

    const auto& framebuffer_info = targets.sceneColorFramebuffer().getFramebufferInfo();
    if (!m_impl->pipeline || m_impl->pipeline_framebuffer_info !=
                                     static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        // 深度测试整个关掉：深度是采样源而不是附件，背景像素靠 shader 里的 discard 剔掉。
        nvrhi::DepthStencilState depth_state;
        depth_state.disableDepthTest().disableDepthWrite().disableStencil();
        nvrhi::RenderState render_state;
        render_state.setDepthStencilState(depth_state);

        nvrhi::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setVertexShader(m_impl->vertex_shader)
                .setPixelShader(m_impl->pixel_shader)
                .setRenderState(render_state)
                .addBindingLayout(m_impl->binding_layout);
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error(
                    "NVRHI failed to create the deferred lighting graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the deferred lighting graphics pipeline");
    }
}

void DeferredLightingPass::record(PassRecordContext& context) {
    if (!m_impl->pipeline || m_impl->frame_slots.empty()) {
        throw std::logic_error("DeferredLightingPass was not prepared.");
    }
    const auto& scene = context.frame().scene();
    auto& commands = context.commands();

    // 收集本帧的光源。禁用的在这里就滤掉 —— 传上去再判断只是浪费带宽和一次分支。
    // GPU 光源缓冲只装 enabled 的灯，所以它的下标和 RenderScene::lights 的下标**不一样**。
    // ShadowTargets 记的是后者，这里边过滤边把它换算成前者 —— 漏了这一步的表现是
    // 「阴影出现在另一个灯的方向上」，而只有一个灯时又恰好正确，非常难查。
    auto& shadows = context.shadows();
    float shadow_light_slot = kNoShadowLight;
    m_impl->lights.clear();
    for (std::size_t index = 0; index < scene.lights.size(); ++index) {
        const auto& light = scene.lights[index];
        if (!light.enabled) {
            continue;
        }
        if (shadows.hasCascades() && index == shadows.shadowLightIndex()) {
            shadow_light_slot = static_cast<float>(m_impl->lights.size());
        }
        m_impl->lights.push_back(toGpuLight(light));
    }
    auto& slot = m_impl->frame_slots.at(context.frameSlotIndex());
    // 必须排在 ensureBindingSet 之前：扩容会换掉缓冲句柄，这个 slot 的绑定集得跟着重建。
    m_impl->ensureLightBuffer(context.device(), slot, m_impl->lights.size());
    m_impl->ensureBindingSet(context, slot);

    LightingConstants constants{};
    const glm::mat4 view_projection = scene.view.projection * scene.view.view;
    const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
    std::memcpy(constants.inverse_view_projection.data(), glm::value_ptr(inverse_view_projection),
            sizeof(constants.inverse_view_projection));
    const glm::vec4 camera{ scene.view.camera_position, 1.0f };
    std::memcpy(constants.camera_position.data(), glm::value_ptr(camera), sizeof(camera));

    constants.light_count = { static_cast<uint32_t>(m_impl->lights.size()), 0, 0, 0 };

    // 环境光来自 RenderScene::environment。有 IBL 时 ambient_color 不参与着色，
    // 但照样填上 —— 烘焙还没就绪的那一帧会退回它。
    const auto& environment = scene.environment;
    const glm::vec4 ambient =
            environment.enabled
                    ? glm::vec4{ glm::vec3{ environment.sky_color } * environment.intensity, 1.0f }
                    : glm::vec4{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::memcpy(constants.ambient_color.data(), glm::value_ptr(ambient), sizeof(ambient));

    const auto& ibl = context.environment();
    const bool use_ibl = environment.enabled && ibl.ready;
    constants.environment_params = { environment.enabled ? environment.intensity : 0.0f,
        use_ibl ? 1.0f : 0.0f, static_cast<float>(ibl.prefiltered_mips), 0.0f };

    // 背景色：没有几何体覆盖的像素输出它。和 ClearScenePass 无关 —— SceneColor 的每个像素
    // 都由这个 pass 写，那边只清深度和 G-Buffer。
    std::memcpy(constants.background_color.data(), glm::value_ptr(scene.clear_color),
            sizeof(constants.background_color));

    commands.writeBuffer(m_impl->constants, &constants, sizeof(constants));

    // 阴影常量。没有阴影的帧也要写：绑定集里那个 buffer 一直在，不写就残留上一帧的矩阵，
    // 而 params.w 的哨兵已经让着色端不去用它了 —— 但留着旧数据没有好处。
    ShadowConstantsBuffer shadow_buffer{};
    shadow_buffer.params = { 0.0f, kShadowFadeStart,
        1.0f / static_cast<float>(kShadowMapResolution), kNoShadowLight };
    if (shadows.hasCascades()) {
        for (uint32_t index = 0; index < kShadowCascadeCount; ++index) {
            const auto& cascade = shadows.cascades()[index];
            std::memcpy(shadow_buffer.light_view_projection[index].data(),
                    glm::value_ptr(cascade.light_view_projection),
                    sizeof(shadow_buffer.light_view_projection[index]));
            shadow_buffer.split_far[index] = cascade.split_far;
        }
        shadow_buffer.params[0] = shadows.shadowDistance();
        shadow_buffer.params[3] = shadow_light_slot;
    }
    commands.writeBuffer(m_impl->shadow_constants, &shadow_buffer, sizeof(shadow_buffer));
    // 只写用到的那一段，缓冲余下的容量保持原样 —— light_count 之外的元素着色器读不到。
    if (!m_impl->lights.empty()) {
        commands.writeBuffer(slot.lights, m_impl->lights.data(),
                m_impl->lights.size() * sizeof(GpuLight));
    }

    auto& framebuffer = context.targets().sceneColorFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&framebuffer)
            .setViewport(viewport)
            .addBindingSet(slot.binding_set);
    commands.setGraphicsState(state);
    // 一个覆盖全屏的三角形。不数进 statistics().draw_calls —— 那个数字的意思是「场景里画了
    // 多少个 submesh」，把固定开销的全屏 pass 混进去会让它失去可比性（Tonemap / Present
    // / Sky 同理都不计）。
    commands.draw(nvrhi::DrawArguments{}.setVertexCount(3));
}

} // namespace arti::rendering
