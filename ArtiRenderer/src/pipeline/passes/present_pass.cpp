#include "present_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "shader_paths.h"

#include <array>
#include <limits>
#include <stdexcept>

namespace arti::rendering {

struct PresentPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    // PSO 只依赖格式和采样数，尺寸变了不用重建。
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // 只在 RenderTargetSet 重建过（离屏纹理换了）时才重建 binding set。用 revision 而不是自己
    // 存纹理指针再比对 —— 目标集自己就告诉你它变了。
    uint64_t bound_revision{ std::numeric_limits<uint64_t>::max() };
    nvrhi::BindingSetHandle binding_set;
};

PresentPass::PresentPass()
        : m_impl(std::make_unique<Impl>()) {}

PresentPass::~PresentPass() = default;

void PresentPass::prepare(PassPrepareContext& context)
{
    auto& device = context.device();
    auto& targets = context.targets();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("present.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(
                device, program, "ArtiRenderer present");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The present shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        nvrhi::SamplerDesc sampler_desc;
        sampler_desc.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge);
        m_impl->sampler = device.createSampler(sampler_desc);
        if (!m_impl->sampler) {
            throw std::runtime_error("NVRHI failed to create the present sampler.");
        }
    }

    // 管线在所有 pass 的 prepare() 之前建好了目标，所以这里读得到。
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
            throw std::runtime_error("NVRHI failed to create the present binding set.");
        }
        m_impl->bound_revision = targets.revision();
    }

    const auto& framebuffer_info = targets.outputFramebuffer().getFramebufferInfo();
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
        // 用 FramebufferInfo 那个重载：吃 IFramebuffer* 的已经标了 [[deprecated]]。
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the present graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
    }
}

void PresentPass::record(PassRecordContext& context)
{
    if (!m_impl->pipeline || !m_impl->binding_set) {
        throw std::logic_error("PresentPass was not prepared.");
    }

    auto& framebuffer = context.targets().outputFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&framebuffer)
            .setViewport(viewport)
            .addBindingSet(m_impl->binding_set);

    auto& commands = context.commands();
    commands.setGraphicsState(state);
    commands.draw(nvrhi::DrawArguments{}.setVertexCount(3));
}

} // namespace arti::rendering
