#include "present_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "shader_paths.h"

#include <array>
#include <stdexcept>

namespace arti::rendering {

struct PresentPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::SamplerHandle sampler;
    nvrhi::GraphicsPipelineHandle pipeline;

    // 只在源纹理换了（离屏目标重建）时才重建 binding set。
    nvrhi::ITexture* bound_source{ nullptr };
    nvrhi::BindingSetHandle binding_set;
};

PresentPass::PresentPass()
    : m_impl(std::make_unique<Impl>())
{}

PresentPass::~PresentPass() = default;

void PresentPass::prepare(PassPrepareContext& context)
{
    auto& device = context.device();

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

    // 上游 pass 必须已经在它自己的 prepare 里登记过 SceneColor。
    auto& source = context.blackboard().require(PassSlot::SceneColor);
    if (m_impl->bound_source != &source || !m_impl->binding_set) {
        const std::array resources = {
            arti::renderer::vulkan::NvrhiBindingResource::Texture("scene_color", source),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("scene_sampler",
                    *m_impl->sampler),
        };
        m_impl->binding_set = arti::renderer::vulkan::createNvrhiBindingSet(
                device, m_impl->reflection, 0, *m_impl->binding_layout, resources);
        if (!m_impl->binding_set) {
            throw std::runtime_error("NVRHI failed to create the present binding set.");
        }
        m_impl->bound_source = &source;
    }

    if (!m_impl->pipeline) {
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
        m_impl->pipeline = device.createGraphicsPipeline(
                pipeline_desc, context.framebuffer().getFramebufferInfo());
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the present graphics pipeline.");
        }
    }
}

void PresentPass::record(PassRecordContext& context)
{
    if (!m_impl->pipeline || !m_impl->binding_set) {
        throw std::logic_error("PresentPass was not prepared.");
    }

    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(context.framebufferInfo().getViewport());

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&context.framebuffer())
            .setViewport(viewport)
            .addBindingSet(m_impl->binding_set);

    auto& commands = context.commands();
    commands.setGraphicsState(state);
    commands.draw(nvrhi::DrawArguments{}.setVertexCount(3));
}

} // namespace arti::rendering
