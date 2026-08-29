#include "tonemap_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "shader_paths.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace arti::rendering {
namespace {

// push constant。只有曝光一个值 —— 曲线的系数是常量，编进着色器就够，没必要跨边界传。
struct TonemapConstants {
    float exposure{ 1.0f };
};

static_assert(std::is_standard_layout_v<TonemapConstants>);
static_assert(sizeof(TonemapConstants) == sizeof(float));

} // namespace

struct TonemapPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    // PSO 只依赖格式和采样数，尺寸变了不用重建。
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // 只在 RenderTargetSet 重建过（离屏纹理换了）时才重建 binding set。
    uint64_t bound_revision{ std::numeric_limits<uint64_t>::max() };
    nvrhi::BindingSetHandle binding_set;
};

TonemapPass::TonemapPass()
        : m_impl(std::make_unique<Impl>()) {}

TonemapPass::~TonemapPass() = default;

void TonemapPass::prepare(PassPrepareContext& context)
{
    auto& device = context.device();
    auto& targets = context.targets();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("tonemap.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(
                device, program, "ArtiRenderer tonemap");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The tonemap shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        // ClampToEdge + 线性过滤。源和目标同尺寸、UV 落在像素中心，所以过滤模式实际不参与
        // 采样结果；夹取模式是为了将来做降采样链时边缘不会绕回去。
        nvrhi::SamplerDesc sampler_desc;
        sampler_desc.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge);
        m_impl->sampler = device.createSampler(sampler_desc);
        if (!m_impl->sampler) {
            throw std::runtime_error("NVRHI failed to create the tonemap sampler.");
        }
    }

    if (m_impl->bound_revision != targets.revision() || !m_impl->binding_set) {
        const std::array resources = {
            arti::renderer::vulkan::NvrhiBindingResource::Texture(
                    "scene_color", targets.sceneColor()),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler(
                    "scene_sampler", *m_impl->sampler),
        };
        m_impl->binding_set = arti::renderer::vulkan::createNvrhiBindingSet(
                device, m_impl->reflection, 0, *m_impl->binding_layout, resources);
        if (!m_impl->binding_set) {
            throw std::runtime_error("NVRHI failed to create the tonemap binding set.");
        }
        m_impl->bound_revision = targets.revision();
    }

    const auto& framebuffer_info = targets.displayFramebuffer().getFramebufferInfo();
    if (!m_impl->pipeline ||
            m_impl->pipeline_framebuffer_info !=
                    static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
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
            throw std::runtime_error("NVRHI failed to create the tonemap graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
    }
}

void TonemapPass::record(PassRecordContext& context)
{
    if (!m_impl->pipeline || !m_impl->binding_set) {
        throw std::logic_error("TonemapPass was not prepared.");
    }

    auto& framebuffer = context.targets().displayFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&framebuffer)
            .setViewport(viewport)
            .addBindingSet(m_impl->binding_set);

    TonemapConstants constants;
    // 非正的曝光会把整个画面压成黑，几乎肯定是调用方填错了而不是刻意的，所以夹到一个下限。
    constants.exposure = std::max(context.frame().scene().exposure, 0.0001f);

    auto& commands = context.commands();
    commands.setGraphicsState(state);
    commands.setPushConstants(&constants, sizeof(constants));
    commands.draw(nvrhi::DrawArguments{}.setVertexCount(3));
}

} // namespace arti::rendering
