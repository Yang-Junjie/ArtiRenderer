#include "shadow_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "draw_resolve.h"
#include "log.h"
#include "mesh_vertex_layout.h"
#include "nvrhi_conversion.h"
#include "shader_paths.h"
#include "shadow_cascades.h"

#include <array>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace arti::rendering {
namespace {

// 逐 draw 的 push constant。只放 model 和 cascade 下标 —— 光源矩阵是逐级的、不是逐 draw 的，
// 塞进来会挤到 Vulkan 保证的 128 字节边界上。
struct ShadowConstants {
    std::array<float, 16> model;
    uint32_t cascade{ 0 };
    // Slang 把 push constant 块的大小向上取整到最大成员的对齐（float4x4 → 16），所以它那边是
    // 80 而不是 68。不补齐 setPushConstants 会被 NVRHI 按大小拒掉 —— picking pass 上踩过一次。
    std::array<uint32_t, 3> padding{};
};

static_assert(std::is_standard_layout_v<ShadowConstants>);
static_assert(sizeof(ShadowConstants) == 80);
static_assert(offsetof(ShadowConstants, cascade) == 64);

// 四级的光源 view-projection，一次全传进去。着色端按 push constant 里的下标取。
struct ShadowCascadeBuffer {
    std::array<std::array<float, 16>, kShadowCascadeCount> light_view_projection{};
};

static_assert(sizeof(ShadowCascadeBuffer) == 64 * kShadowCascadeCount);

// 这一帧投影的那个方向光。没有就返回 nullopt。
//
// 只取第一个：多方向光各带一套 cascade 会让显存和 pass 数翻倍。第二个想投影的方向光会被忽略，
// 那件事值得说一声，否则场景里放两个太阳的人会以为是 bug。
struct ShadowLight {
    LightDesc light;
    uint32_t index{ 0 };
};

std::optional<ShadowLight> shadowCastingLight(const RenderScene& scene) {
    std::optional<ShadowLight> found;
    uint32_t ignored = 0;
    for (uint32_t index = 0; index < scene.lights.size(); ++index) {
        const auto& light = scene.lights[index];
        if (light.type != LightType::Directional || !light.enabled || !light.casts_shadow) {
            continue;
        }
        if (found) {
            ++ignored;
            continue;
        }
        found = ShadowLight{ light, index };
    }
    if (ignored > 0) {
        getLogChannel().warn("{} extra shadow-casting directional light(s) ignored; "
                             "only the first one casts shadows",
                ignored);
    }
    return found;
}

} // namespace

struct ShadowPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::BindingSetHandle binding_set;
    nvrhi::InputLayoutHandle input_layout;
    nvrhi::BufferHandle cascade_buffer;

    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::FramebufferInfo pipeline_framebuffer_info;
};

ShadowPass::ShadowPass() : m_impl(std::make_unique<Impl>()) {}

ShadowPass::~ShadowPass() = default;

bool ShadowPass::isEnabled(const FrameContext& frame) const {
    const auto& scene = frame.scene();
    if (scene.draws.empty()) {
        return false;
    }
    // 这里不打 warn：isEnabled 每帧都问，重复的日志会淹掉别的东西。真正的 warn 在 record()。
    for (const auto& light: scene.lights) {
        if (light.type == LightType::Directional && light.enabled && light.casts_shadow) {
            return true;
        }
    }
    return false;
}

void ShadowPass::prepare(PassPrepareContext& context) {
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("shadow_depth.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(device, program,
                "ArtiRenderer shadow depth");
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->reflection = program.reflection;
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The shadow depth shader has no NVRHI binding layout.");
        }
        m_impl->binding_layout = shaders.binding_layouts.front();

        nvrhi::BufferDesc cascade_desc;
        cascade_desc.setByteSize(sizeof(ShadowCascadeBuffer))
                .setIsConstantBuffer(true)
                .setDebugName("ArtiRenderer ShadowCascadeBuffer")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
        m_impl->cascade_buffer = device.createBuffer(cascade_desc);
        if (!m_impl->cascade_buffer) {
            throw std::runtime_error("NVRHI failed to create the shadow cascade buffer.");
        }

        const std::array resources{
            arti::renderer::vulkan::NvrhiBindingResource::Buffer("shadow_cascades",
                    *m_impl->cascade_buffer),
        };
        m_impl->binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device,
                m_impl->reflection, 0, *m_impl->binding_layout, resources);
        if (!m_impl->binding_set) {
            throw std::runtime_error("NVRHI failed to create the shadow binding set.");
        }

        // 只要 position。顶点缓冲的布局是共享的，所以 stride 得对上 —— 用同一张布局表只取
        // 第一个属性，和 PickingPass 一个做法。
        const auto attributes = detail::toNvrhiAttributes(detail::meshVertexLayout());
        if (attributes.empty()) {
            throw std::runtime_error("The mesh vertex layout has no attributes.");
        }
        m_impl->input_layout =
                device.createInputLayout(attributes.data(), 1, m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the shadow input layout.");
        }
    }

    // 四级的 framebuffer info 完全一样（同一张 array、同样的尺寸和格式），所以 PSO 只建一次。
    const auto& framebuffer_info = context.shadows().framebuffer(0).getFramebufferInfo();
    if (!m_impl->pipeline || m_impl->pipeline_framebuffer_info !=
                                     static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        nvrhi::DepthStencilState depth_state;
        depth_state.setDepthTestEnable(true)
                .setDepthWriteEnable(true)
                .setDepthFunc(nvrhi::ComparisonFunc::Less)
                .disableStencil();

        nvrhi::RasterState raster_state;
        // 标准背面剔除。**不做正面剔除** —— 那招看起来能消 acne，但法向不规范的物体会直接出
        // 瑕疵，而且墙根这类地方 peter-panning 和阴影缝隙更容易出现（深度差太小）。
        // 消 acne 交给 slope-scaled bias（阶段 5）。
        raster_state.setCullBack().setFrontCounterClockwise(true);
        // 关掉深度裁剪 = 打开深度钳制：位于这一级 near 平面之前的投影体会被压到 0 而不是被
        // 剪掉。near 已经按投影体的范围往回拉过了，这条是兜底 —— 少了它，某些角度下挡在光和
        // 物体之间的东西会突然不投影。
        raster_state.disableDepthClip();
        // 消 shadow acne：**slope-scaled 为主，常数项为辅**。
        //
        // acne 的根因是「写进阴影图的深度」和「采样时算出的深度」对同一个表面点只相等到精度为止，
        // 于是比较结果在两边随机跳；掠射角（表面几乎平行于光线）下一个 texel 覆盖的深度跨度最大，
        // 所以偏移必须跟着斜率走 —— 这正是 slope-scaled 的定义。
        //
        // 常数项给得越大越容易换来 peter-panning（阴影和物体在接触处脱开），所以先调 slope、
        // 常数项只给一点点。这两个数是**看画面调出来的**，换分辨率或换级数都可能要重调。
        raster_state.setSlopeScaleDepthBias(2.0f).setDepthBias(1);

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
            throw std::runtime_error("NVRHI failed to create the shadow graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the shadow depth graphics pipeline");
    }
}

void ShadowPass::record(PassRecordContext& context) {
    if (!m_impl->pipeline) {
        throw std::logic_error("ShadowPass was not prepared.");
    }

    const auto& scene = context.frame().scene();
    const auto light = shadowCastingLight(scene);
    if (!light) {
        return;
    }

    auto& shadows = context.shadows();
    const auto computed = detail::computeShadowCascades(scene.view, light->light, scene.draws);
    shadows.setCascades(computed.cascades, computed.shadow_distance, light->index);

    ShadowCascadeBuffer buffer{};
    for (uint32_t index = 0; index < kShadowCascadeCount; ++index) {
        std::memcpy(buffer.light_view_projection[index].data(),
                glm::value_ptr(computed.cascades[index].light_view_projection),
                sizeof(buffer.light_view_projection[index]));
    }

    auto& commands = context.commands();
    commands.writeBuffer(m_impl->cascade_buffer, &buffer, sizeof(buffer));

    for (uint32_t index = 0; index < kShadowCascadeCount; ++index) {
        auto& framebuffer = shadows.framebuffer(index);
        const auto& framebuffer_info = framebuffer.getFramebufferInfo();

        // 每级自己清：这张深度只在有投影光源的帧才有内容，让 ClearScenePass 去管它就成了
        // 隐式耦合。清成 1.0 = 远平面。
        commands.clearDepthStencilTexture(&shadows.depthArray(),
                nvrhi::TextureSubresourceSet().setArraySlices(index, 1), true, 1.0f, false, 0);

        nvrhi::ViewportState viewport;
        viewport.addViewportAndScissorRect(framebuffer_info.getViewport());

        for (std::size_t draw_index = 0; draw_index < scene.draws.size(); ++draw_index) {
            const DrawItem& draw = scene.draws[draw_index];
            const auto resolved = detail::resolveDraw(context.frame(), draw);
            // 跳过条件和 GBufferPass 逐条对齐（材质类型），但**可见性不是相机视锥**。
            // 投射体可以完全在画面外而影子在画面内 —— 拿相机视锥剔阴影会让那些影子凭空消失，
            // 而且只在特定相机角度下。这里读的是 computeShadowCascades 算好的光空间 XY 重叠。
            if (!resolved || resolved->material.type != MaterialType::PBR) {
                continue;
            }
            if (!computed.isCasterVisible(index, draw_index)) {
                ++context.frame().statistics().shadow_culled;
                continue;
            }

            ShadowConstants constants{};
            std::memcpy(constants.model.data(), glm::value_ptr(draw.transform),
                    sizeof(constants.model));
            constants.cascade = index;

            nvrhi::GraphicsState state;
            state.setPipeline(m_impl->pipeline)
                    .setFramebuffer(&framebuffer)
                    .setViewport(viewport)
                    .addBindingSet(m_impl->binding_set)
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
        }
    }
}

} // namespace arti::rendering
