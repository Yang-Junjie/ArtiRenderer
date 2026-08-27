#include "blinn_phong_opaque_pass.h"

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

// push constant。Vulkan 只保证 128 字节可用，这里 96。
//
// 刻意不传 MVP：那样 model(64) + MVP(64) 就已经把 128 占满，材质参数无处可放。改为只传 model，
// view_projection 放进逐帧的 UBO，MVP 在顶点着色器里算。
struct BlinnPhongDrawConstants {
    std::array<float, 16> model;
    std::array<float, 4> base_color;
    // xyz = specular_color * specular_strength，w = shininess
    std::array<float, 4> specular;
};

static_assert(std::is_standard_layout_v<BlinnPhongDrawConstants>);
static_assert(sizeof(BlinnPhongDrawConstants) == sizeof(float) * 24);
static_assert(sizeof(BlinnPhongDrawConstants) <= 128,
        "Vulkan only guarantees 128 push constant bytes.");

// 逐帧常量。全部 float4 / float4x4，HLSL 与 std140 的打包规则因此一致，两侧不用对 padding 猜谜。
struct BlinnPhongFrameConstants {
    std::array<float, 16> view_projection;
    std::array<float, 4> camera_position;
    std::array<float, 4> light_direction;
    std::array<float, 4> light_color;
    std::array<float, 4> ambient_color;
};

static_assert(std::is_standard_layout_v<BlinnPhongFrameConstants>);
static_assert(sizeof(BlinnPhongFrameConstants) == sizeof(float) * 32);

// 场景里第一个启用的方向光。没有就返回 nullptr，此时只剩环境光。
const LightDesc* findDirectionalLight(const RenderScene& scene)
{
    for (const auto& light: scene.lights) {
        if (light.enabled && light.type == LightType::Directional) {
            return &light;
        }
    }
    return nullptr;
}

} // namespace

struct BlinnPhongOpaquePass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::InputLayoutHandle input_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // 逐帧常量，每帧 writeBuffer 一次，所有 draw 共用。非 volatile，理由见创建处。
    nvrhi::BufferHandle frame_constants;

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
            arti::renderer::vulkan::NvrhiBindingResource::Buffer(
                    "frame_constants", *frame_constants),
        };
        auto binding_set = arti::renderer::vulkan::createNvrhiBindingSet(
                context.device(), reflection, 0, *binding_layout, resources);
        if (!binding_set) {
            throw std::runtime_error("NVRHI failed to create a Blinn-Phong binding set.");
        }
        return *binding_sets.emplace(texture, std::move(binding_set)).first->second;
    }
};

BlinnPhongOpaquePass::BlinnPhongOpaquePass()
    : m_impl(std::make_unique<Impl>())
{}

BlinnPhongOpaquePass::~BlinnPhongOpaquePass() = default;

void BlinnPhongOpaquePass::prepare(PassPrepareContext& context)
{
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("forward_blinn_phong.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(
                device, program, "ArtiRenderer forward Blinn-Phong");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The Blinn-Phong shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        m_impl->sampler = device.createSampler(nvrhi::SamplerDesc{});
        if (!m_impl->sampler) {
            throw std::runtime_error("NVRHI failed to create the Blinn-Phong sampler.");
        }

        // 刻意**不用** volatile。BindingSetItem::ConstantBuffer 会从 buffer 自动判定 volatile
        // 并发出 VolatileConstantBuffer，但 binding layout 是从 shader 反射来的，反射只看得到
        // 着色器、看不到 buffer 是不是 volatile，只能发出普通 ConstantBuffer。两者不一致会让
        // Vulkan 报 UNIFORM_BUFFER_DYNAMIC vs UNIFORM_BUFFER 的 descriptor type 错误。
        //
        // 非 volatile 的 constant buffer 用 writeBuffer 走 upload manager 写入，效果一样。
        nvrhi::BufferDesc frame_desc;
        frame_desc.setByteSize(sizeof(BlinnPhongFrameConstants))
                .setIsConstantBuffer(true)
                .setDebugName("ArtiRenderer BlinnPhongFrameConstants")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
        m_impl->frame_constants = device.createBuffer(frame_desc);
        if (!m_impl->frame_constants) {
            throw std::runtime_error(
                    "NVRHI failed to create the Blinn-Phong frame constant buffer.");
        }

        const auto attributes = detail::toNvrhiAttributes(detail::meshVertexLayout());
        m_impl->input_layout = device.createInputLayout(
                attributes.data(), static_cast<uint32_t>(attributes.size()),
                m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the Blinn-Phong input layout.");
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
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the Blinn-Phong graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the Blinn-Phong graphics pipeline");
    }
}

void BlinnPhongOpaquePass::record(PassRecordContext& context)
{
    auto& frame = context.frame();
    const auto& scene = frame.scene();
    auto& commands = context.commands();

    auto& framebuffer = context.targets().sceneFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    // 逐帧常量写一次，所有 draw 共用。
    const glm::mat4 view_projection = scene.view.projection * scene.view.view;
    BlinnPhongFrameConstants frame_constants{};
    std::memcpy(frame_constants.view_projection.data(), glm::value_ptr(view_projection),
            sizeof(frame_constants.view_projection));
    const glm::vec4 camera{ scene.view.camera_position, 1.0f };
    std::memcpy(frame_constants.camera_position.data(), glm::value_ptr(camera), sizeof(camera));

    if (const auto* light = findDirectionalLight(scene)) {
        // LightDesc::direction 是光的传播方向；着色需要的是从表面指向光源，所以取反。
        const glm::vec3 to_light = glm::normalize(-light->direction);
        const glm::vec4 direction{ to_light, 0.0f };
        std::memcpy(frame_constants.light_direction.data(), glm::value_ptr(direction),
                sizeof(direction));
        const glm::vec4 color{ glm::vec3{ light->color }, light->intensity };
        std::memcpy(frame_constants.light_color.data(), glm::value_ptr(color), sizeof(color));
    }
    // 没有方向光时 light_color 保持全 0，只剩环境光贡献。

    // 环境光目前是个固定的小常量。RenderScene 还没有环境光字段，等真需要再提上去。
    constexpr glm::vec4 ambient{ 0.03f, 0.03f, 0.035f, 1.0f };
    std::memcpy(frame_constants.ambient_color.data(), glm::value_ptr(ambient), sizeof(ambient));

    commands.writeBuffer(m_impl->frame_constants, &frame_constants, sizeof(frame_constants));

    for (const auto& draw: scene.draws) {
        const auto resolved = detail::resolveDraw(frame, draw);
        if (!resolved) {
            continue;
        }
        if (resolved->material.type != MaterialType::BlinnPhong) {
            continue;
        }

        BlinnPhongDrawConstants constants{};
        std::memcpy(constants.model.data(), glm::value_ptr(draw.transform),
                sizeof(constants.model));
        std::memcpy(constants.base_color.data(), glm::value_ptr(resolved->material.base_color),
                sizeof(constants.base_color));

        // specular_strength 在 CPU 侧就乘进颜色，着色器少一次乘法也少一个参数。
        const glm::vec4 specular{
            resolved->material.specular_color * resolved->material.specular_strength,
            resolved->material.shininess
        };
        std::memcpy(constants.specular.data(), glm::value_ptr(specular), sizeof(specular));

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
