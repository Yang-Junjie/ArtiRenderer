#include "deferred_lighting_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "log.h"
#include "shader_paths.h"

#include <array>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace arti::rendering {
namespace {

// 全部走 UBO 而不是 push constant：64 + 6×16 = 160 字节，远超 Vulkan 保证的 128。
// 全是 float4 / float4x4，HLSL 与 std140 的打包规则因此一致，两侧不用对 padding 猜谜。
struct LightingConstants {
    std::array<float, 16> inverse_view_projection;
    std::array<float, 4> camera_position;
    std::array<float, 4> light_direction;
    std::array<float, 4> light_color;
    std::array<float, 4> ambient_color;
    // x = 环境强度，y = IBL 是否就绪，z = prefiltered mip 数，w 未用
    std::array<float, 4> environment_params;
    // RenderScene::clear_color。没有几何体覆盖的像素直接输出它 —— 这个 pass 是 SceneColor
    // 每个像素的唯一写入者，所以背景色也得由它来写。
    std::array<float, 4> background_color;
};

static_assert(std::is_standard_layout_v<LightingConstants>);
static_assert(sizeof(LightingConstants) == sizeof(float) * 40);

// 场景里第一个启用的方向光。没有就返回 nullptr，此时只剩环境光。
//
// 只取一个是当下的实现上限，不是延迟渲染的限制 —— 换成一个光源列表（StructuredBuffer + 循环）
// 是这条管线真正的收益所在，而它需要引擎侧先有点光源和聚光灯组件。
const LightDesc* findDirectionalLight(const RenderScene& scene) {
    for (const auto& light: scene.lights) {
        if (light.enabled && light.type == LightType::Directional) {
            return &light;
        }
    }
    return nullptr;
}

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

    // G-Buffer 重建（改尺寸）或者环境重烘（IBL 三件套换了）都要重建绑定集，所以两个 revision
    // 都记一份。初值取 max 是为了第一帧一定不相等。
    uint64_t bound_gbuffer_revision{ std::numeric_limits<uint64_t>::max() };
    uint64_t bound_environment_revision{ std::numeric_limits<uint64_t>::max() };
    nvrhi::BindingSetHandle binding_set;

    // 绑定集在 **record()** 里建，不在 prepare()：EnvironmentResources 的那几个句柄要到
    // EnvironmentBakePass::record() 才填上（连兜底的 1x1 黑图也是在那里发布的），prepare
    // 阶段读到的还是空句柄，建集会直接抛。EnvironmentBake < Lighting 这个 stage 顺序保证了
    // 到这里它们一定有效。
    void ensureBindingSet(PassRecordContext& context) {
        auto& device = context.device();
        auto& targets = context.targets();
        auto& gbuffer = context.gbuffer();
        const auto& environment = context.environment();
        if (binding_set && bound_gbuffer_revision == gbuffer.revision() &&
                bound_environment_revision == environment.revision) {
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
        };
        binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device, reflection, 0,
                *binding_layout, resources);
        if (!binding_set) {
            throw std::runtime_error("NVRHI failed to create the deferred lighting binding set.");
        }
        bound_gbuffer_revision = gbuffer.revision();
        bound_environment_revision = environment.revision;
    }
};

DeferredLightingPass::DeferredLightingPass()
        : m_impl(std::make_unique<Impl>()) {}

DeferredLightingPass::~DeferredLightingPass() = default;

void DeferredLightingPass::prepare(PassPrepareContext& context) {
    auto& device = context.device();
    auto& targets = context.targets();

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
    if (!m_impl->pipeline) {
        throw std::logic_error("DeferredLightingPass was not prepared.");
    }
    m_impl->ensureBindingSet(context);

    const auto& scene = context.frame().scene();
    auto& commands = context.commands();

    LightingConstants constants{};
    const glm::mat4 view_projection = scene.view.projection * scene.view.view;
    const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
    std::memcpy(constants.inverse_view_projection.data(), glm::value_ptr(inverse_view_projection),
            sizeof(constants.inverse_view_projection));
    const glm::vec4 camera{ scene.view.camera_position, 1.0f };
    std::memcpy(constants.camera_position.data(), glm::value_ptr(camera), sizeof(camera));

    if (const auto* light = findDirectionalLight(scene)) {
        // LightDesc::direction 是光的传播方向；着色需要的是从表面指向光源，所以取反。
        const glm::vec3 to_light = glm::normalize(-light->direction);
        const glm::vec4 direction{ to_light, 0.0f };
        std::memcpy(constants.light_direction.data(), glm::value_ptr(direction), sizeof(direction));
        const glm::vec4 color{ glm::vec3{ light->color }, light->intensity };
        std::memcpy(constants.light_color.data(), glm::value_ptr(color), sizeof(color));
    }
    // 没有方向光时 light_color 保持全 0，只剩环境光贡献。

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

    auto& framebuffer = context.targets().sceneColorFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&framebuffer)
            .setViewport(viewport)
            .addBindingSet(m_impl->binding_set);
    commands.setGraphicsState(state);
    // 一个覆盖全屏的三角形。不数进 statistics().draw_calls —— 那个数字的意思是「场景里画了
    // 多少个 submesh」，把固定开销的全屏 pass 混进去会让它失去可比性（Tonemap / Present
    // / Sky 同理都不计）。
    commands.draw(nvrhi::DrawArguments{}.setVertexCount(3));
}

} // namespace arti::rendering
