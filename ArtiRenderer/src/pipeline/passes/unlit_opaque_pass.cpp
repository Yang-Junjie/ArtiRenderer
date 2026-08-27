#include "unlit_opaque_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "draw_resolve.h"
#include "log.h"
#include "mesh_vertex_layout.h"
#include "nvrhi_conversion.h"
#include "shader_paths.h"

#include <array>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace arti::rendering {
namespace {

// unlit 不读光照，所以 view_projection 直接乘进 MVP，不需要逐帧 UBO。80 字节。
struct UnlitDrawConstants {
    std::array<float, 16> model_view_projection;
    std::array<float, 4> base_color;
};

static_assert(std::is_standard_layout_v<UnlitDrawConstants>);
static_assert(sizeof(UnlitDrawConstants) == sizeof(float) * 20);
static_assert(sizeof(UnlitDrawConstants) <= 128, "Vulkan only guarantees 128 push constant bytes.");

} // namespace

struct UnlitOpaquePass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::InputLayoutHandle input_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    // PSO 只依赖 attachment 的格式和采样数，不依赖尺寸（FramebufferInfo 里没有宽高，而且带
    // operator==），所以窗口缩放不需要重建。
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // TextureHandle -> binding set。只依赖 binding layout 和纹理，可以跨帧缓存。
    std::unordered_map<TextureHandle, nvrhi::BindingSetHandle> binding_sets;

    nvrhi::IBindingSet& bindingSetFor(PassRecordContext& context, TextureHandle texture)
    {
        const auto cached = binding_sets.find(texture);
        if (cached != binding_sets.end()) {
            return *cached->second;
        }

        const std::array resources = {
            arti::renderer::vulkan::NvrhiBindingResource::Texture(
                    "base_color_texture", context.texture(texture)),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler(
                    "base_color_sampler", *sampler),
        };
        auto binding_set = arti::renderer::vulkan::createNvrhiBindingSet(
                context.device(), reflection, 0, *binding_layout, resources);
        if (!binding_set) {
            throw std::runtime_error("NVRHI failed to create an unlit binding set.");
        }
        return *binding_sets.emplace(texture, std::move(binding_set)).first->second;
    }
};

UnlitOpaquePass::UnlitOpaquePass()
    : m_impl(std::make_unique<Impl>())
{}

UnlitOpaquePass::~UnlitOpaquePass() = default;

void UnlitOpaquePass::prepare(PassPrepareContext& context)
{
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("forward_unlit.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(
                device, program, "ArtiRenderer forward unlit");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The forward unlit shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        m_impl->sampler = device.createSampler(nvrhi::SamplerDesc{});
        if (!m_impl->sampler) {
            throw std::runtime_error("NVRHI failed to create the unlit sampler.");
        }

        const auto attributes = detail::toNvrhiAttributes(detail::meshVertexLayout());
        m_impl->input_layout = device.createInputLayout(
                attributes.data(), static_cast<uint32_t>(attributes.size()),
                m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the unlit input layout.");
        }
    }

    const auto& framebuffer_info = context.targets().sceneFramebuffer().getFramebufferInfo();
    if (!m_impl->pipeline ||
            m_impl->pipeline_framebuffer_info !=
                    static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        nvrhi::DepthStencilState depth_state;
        depth_state.enableDepthTest().enableDepthWrite().disableStencil();
        nvrhi::RasterState raster_state;
        // 网格按「从外面看逆时针 = 正面」的常规约定编写。这个值是实测定下来的：把剔除关掉
        // （深度测试自然给出正确图像）当基准，true 与基准逐位一致，false 会把正面剔掉。
        raster_state.setCullBack().setFrontCounterClockwise(true);
        nvrhi::RenderState render_state;
        render_state.setDepthStencilState(depth_state).setRasterState(raster_state);

        nvrhi::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setInputLayout(m_impl->input_layout)
                .setVertexShader(m_impl->vertex_shader)
                .setPixelShader(m_impl->pixel_shader)
                .setRenderState(render_state)
                .addBindingLayout(m_impl->binding_layout);
        // 用 FramebufferInfo 那个重载：吃 IFramebuffer* 的已经标了 [[deprecated]]。
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the unlit graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the unlit graphics pipeline");
    }
}

void UnlitOpaquePass::record(PassRecordContext& context)
{
    auto& frame = context.frame();
    const auto& scene = frame.scene();
    auto& commands = context.commands();

    auto& framebuffer = context.targets().sceneFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    const glm::mat4 view_projection = scene.view.projection * scene.view.view;

    for (const auto& draw: scene.draws) {
        const auto resolved = detail::resolveDraw(frame, draw);
        if (!resolved) {
            continue;
        }
        // 只画自己那种材质。类型由 PSO 表达，不再需要 shader 里的分支开关。
        if (resolved->material.type != MaterialType::Unlit) {
            continue;
        }

        UnlitDrawConstants constants{};
        const glm::mat4 mvp = view_projection * draw.transform;
        std::memcpy(constants.model_view_projection.data(), glm::value_ptr(mvp),
                sizeof(constants.model_view_projection));
        std::memcpy(constants.base_color.data(), glm::value_ptr(resolved->material.base_color),
                sizeof(constants.base_color));

        nvrhi::GraphicsState state;
        state.setPipeline(m_impl->pipeline)
                .setFramebuffer(&framebuffer)
                .setViewport(viewport)
                .addBindingSet(&m_impl->bindingSetFor(
                        context, resolved->material.base_color_texture))
                .addVertexBuffer(nvrhi::VertexBufferBinding()
                                .setBuffer(&context.vertexBuffer(draw.mesh))
                                .setSlot(0))
                .setIndexBuffer(nvrhi::IndexBufferBinding()
                                .setBuffer(&context.indexBuffer(draw.mesh))
                                .setFormat(detail::toNvrhiIndexFormat(
                                        resolved->mesh->index_buffer.indexType())));
        commands.setGraphicsState(state);
        commands.setPushConstants(&constants, sizeof(constants));
        commands.drawIndexed(nvrhi::DrawArguments{}
                        .setVertexCount(resolved->submesh->index_count)
                        .setStartIndexLocation(resolved->submesh->index_offset)
                        .setStartVertexLocation(resolved->submesh->vertex_offset));

        ++frame.statistics().draw_calls;
        ++frame.statistics().submeshes;
    }
}

} // namespace arti::rendering
