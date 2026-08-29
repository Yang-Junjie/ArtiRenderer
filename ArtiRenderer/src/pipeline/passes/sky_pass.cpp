#include "sky_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
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

struct SkyConstants {
    std::array<float, 16> inverse_view_projection;
    // xyz = 相机世界坐标，w = 环境强度
    std::array<float, 4> camera_position_intensity;
};

static_assert(std::is_standard_layout_v<SkyConstants>);
static_assert(sizeof(SkyConstants) == sizeof(float) * 20);

} // namespace

struct SkyPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    nvrhi::BufferHandle constants;

    // 环境重烘之后 cube 换了，binding set 要跟着重建。
    uint64_t bound_revision{ std::numeric_limits<uint64_t>::max() };
    nvrhi::BindingSetHandle binding_set;
};

SkyPass::SkyPass()
        : m_impl(std::make_unique<Impl>()) {}

SkyPass::~SkyPass() = default;

bool SkyPass::isEnabled(const FrameContext& frame) const
{
    const auto& environment = frame.scene().environment;
    // 烘焙是否就绪在这里看不到（isEnabled 只拿得到 FrameContext），所以那一条留给
    // prepare/record 里判断 —— 反正没就绪时那两个函数都会直接返回。
    return environment.enabled && environment.sky_visible &&
            environment.equirectangular_texture.isValid();
}

void SkyPass::prepare(PassPrepareContext& context)
{
    auto& environment = context.environment();
    if (!environment.ready) {
        return;
    }
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("skybox.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(
                device, program, "ArtiRenderer sky");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The sky shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        nvrhi::BufferDesc desc;
        desc.setByteSize(sizeof(SkyConstants))
                .setIsConstantBuffer(true)
                .setDebugName("ArtiRenderer SkyConstants")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
        m_impl->constants = device.createBuffer(desc);
        if (!m_impl->constants) {
            throw std::runtime_error("NVRHI failed to create the sky constant buffer.");
        }
    }

    if (m_impl->bound_revision != environment.revision || !m_impl->binding_set) {
        const std::array resources = {
            arti::renderer::vulkan::NvrhiBindingResource::Buffer(
                    "sky_constants", *m_impl->constants),
            arti::renderer::vulkan::NvrhiBindingResource::Texture(
                    "environment_cube", *environment.environment),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler(
                    "environment_sampler", *environment.sampler),
        };
        m_impl->binding_set = arti::renderer::vulkan::createNvrhiBindingSet(
                device, m_impl->reflection, 0, *m_impl->binding_layout, resources);
        if (!m_impl->binding_set) {
            throw std::runtime_error("NVRHI failed to create the sky binding set.");
        }
        m_impl->bound_revision = environment.revision;
    }

    const auto& framebuffer_info = context.targets().sceneFramebuffer().getFramebufferInfo();
    if (!m_impl->pipeline ||
            m_impl->pipeline_framebuffer_info !=
                    static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        nvrhi::DepthStencilState depth_state;
        // 测试但不写：天空的深度写进去会污染 PickingPass 复用的那份深度。
        // LessOrEqual 而不是 Less —— 天空的 z 就是清屏值 1.0，用 Less 会被自己挡掉，
        // 结果是整个天空一个像素都不画。
        depth_state.enableDepthTest()
                .disableDepthWrite()
                .disableStencil()
                .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
        nvrhi::RasterState raster_state;
        raster_state.setCullNone();
        nvrhi::RenderState render_state;
        render_state.setDepthStencilState(depth_state).setRasterState(raster_state);

        nvrhi::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setVertexShader(m_impl->vertex_shader)
                .setPixelShader(m_impl->pixel_shader)
                .setRenderState(render_state)
                .addBindingLayout(m_impl->binding_layout);
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the sky graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
    }
}

void SkyPass::record(PassRecordContext& context)
{
    if (!context.environment().ready || !m_impl->pipeline || !m_impl->binding_set) {
        return;
    }

    const auto& scene = context.frame().scene();
    auto& framebuffer = context.targets().sceneFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    const glm::mat4 view_projection = scene.view.projection * scene.view.view;
    const glm::mat4 inverse_view_projection = glm::inverse(view_projection);
    SkyConstants constants{};
    std::memcpy(constants.inverse_view_projection.data(),
            glm::value_ptr(inverse_view_projection), sizeof(constants.inverse_view_projection));
    const glm::vec4 camera{ scene.view.camera_position, scene.environment.intensity };
    std::memcpy(constants.camera_position_intensity.data(), glm::value_ptr(camera),
            sizeof(camera));

    auto& commands = context.commands();
    commands.writeBuffer(m_impl->constants, &constants, sizeof(constants));

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&framebuffer)
            .setViewport(viewport)
            .addBindingSet(m_impl->binding_set);
    commands.setGraphicsState(state);
    commands.draw(nvrhi::DrawArguments{}.setVertexCount(3));
}

} // namespace arti::rendering
