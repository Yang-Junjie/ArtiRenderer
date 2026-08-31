#include "picking_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "draw_resolve.h"
#include "log.h"
#include "mesh_vertex_layout.h"
#include "nvrhi_conversion.h"
#include "shader_paths.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace arti::rendering {
namespace {

// MVP 在 CPU 侧乘好而不是像 GBufferPass 那样分开传 —— 拾取不需要世界坐标，
// 所以没必要为它建一个逐帧 UBO。
struct PickingConstants {
    std::array<float, 16> model_view_projection;
    uint32_t picking_id;
    // Slang 把 push constant 块的大小向上取整到最大成员的对齐（float4x4 → 16），
    // 所以它那边是 80 字节而不是 68。不补齐 setPushConstants 会被 NVRHI 按大小拒掉 ——
    // ImGui pass 上踩过一次（20 → 24），这里提前补。实际值由 reflection 校验。
    std::array<uint32_t, 3> padding{};
};

static_assert(std::is_standard_layout_v<PickingConstants>);
static_assert(sizeof(PickingConstants) == 80);
static_assert(offsetof(PickingConstants, picking_id) == 64);

constexpr auto pickingIdFormat = nvrhi::Format::R32_UINT;

} // namespace

struct PickingPass::Impl {
    // 每个飞行帧一份：这一帧发起的拷贝要等几帧才 map，共用一份会读到别人的。
    struct ReadbackSlot {
        nvrhi::StagingTextureHandle staging;
        bool pending{ false };
        uint32_t x{ 0 };
        uint32_t y{ 0 };
    };

    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::BindingSetHandle binding_set;
    nvrhi::InputLayoutHandle input_layout;

    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // ID 缓冲和它的 framebuffer。深度是借 RenderTargetSet 的，不自己建 ——
    // 借的那张必须和 GBuffer 写的是同一张，否则测试出来的可见性就不是屏幕上那个。
    nvrhi::TextureHandle id_texture;
    nvrhi::FramebufferHandle framebuffer;
    uint64_t bound_revision{ std::numeric_limits<uint64_t>::max() };

    std::vector<ReadbackSlot> slots;
    std::optional<PickResult> ready_result;

    void ensureTargets(nvrhi::IDevice& device, RenderTargetSet& targets) {
        // 跟着 RenderTargetSet 的 revision 失效：场景目标重建（尺寸变了）之后
        // 旧的 ID 缓冲尺寸不对，framebuffer 里的深度附件也已经作废。
        if (framebuffer && bound_revision == targets.revision()) {
            return;
        }

        const auto& scene_info = targets.sceneFramebuffer().getFramebufferInfo();
        nvrhi::TextureDesc desc;
        desc.setWidth(scene_info.width)
                .setHeight(scene_info.height)
                .setFormat(pickingIdFormat)
                .setIsRenderTarget(true)
                .setDebugName("ArtiRenderer PickingId")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        id_texture = device.createTexture(desc);
        if (!id_texture) {
            throw std::runtime_error("NVRHI failed to create the picking ID texture.");
        }

        nvrhi::FramebufferDesc framebuffer_desc;
        framebuffer_desc.addColorAttachment(id_texture).setDepthAttachment(&targets.sceneDepth());
        framebuffer = device.createFramebuffer(framebuffer_desc);
        if (!framebuffer) {
            throw std::runtime_error("NVRHI failed to create the picking framebuffer.");
        }

        bound_revision = targets.revision();
        getLogChannel().debug("Picking targets rebuilt at {}x{} (revision {})", scene_info.width,
                scene_info.height, bound_revision);
    }
};

PickingPass::PickingPass()
        : m_impl(std::make_unique<Impl>()) {}

PickingPass::~PickingPass() = default;

bool PickingPass::isEnabled(const FrameContext& frame) const {
    // 有请求才跑。也就是说不点鼠标的帧完全不付代价，第一次点击之前连 shader 都没编译。
    //
    // 但已经在飞的读回要收尾，否则那个 slot 永远 pending，结果也就永远取不到。
    if (frame.settings().pick.has_value()) {
        return true;
    }
    return std::ranges::any_of(m_impl->slots,
            [](const Impl::ReadbackSlot& slot) { return slot.pending; });
}

void PickingPass::prepare(PassPrepareContext& context) {
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("picking.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(device, program,
                "ArtiRenderer picking");
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->reflection = program.reflection;

        // shader 只有 push constant，没有纹理也没有 UBO，所以 binding layout 里只有
        // push constant 那一项。反射照样会给出一个 layout，binding set 必须建 ——
        // 否则 setGraphicsState 会因为 layout 和 set 数量不匹配而报错。
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The picking shader has no NVRHI binding layout.");
        }
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device,
                m_impl->reflection, 0, *m_impl->binding_layout, {});
        if (!m_impl->binding_set) {
            throw std::runtime_error("NVRHI failed to create the picking binding set.");
        }

        // 只要 position。法线/切线/UV 对拾取没用，但顶点缓冲的布局是共享的，
        // 所以 stride 得对上 —— 用同一张布局表只取第一个属性。
        const auto attributes = detail::toNvrhiAttributes(detail::meshVertexLayout());
        if (attributes.empty()) {
            throw std::runtime_error("The mesh vertex layout has no attributes.");
        }
        m_impl->input_layout =
                device.createInputLayout(attributes.data(), 1, m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the picking input layout.");
        }

        nvrhi::TextureDesc staging_desc;
        staging_desc.setWidth(1)
                .setHeight(1)
                .setFormat(pickingIdFormat)
                .setDebugName("ArtiRenderer PickingId staging");
        m_impl->slots.resize(context.frameSlotCount());
        for (auto& slot: m_impl->slots) {
            slot.staging = device.createStagingTexture(staging_desc, nvrhi::CpuAccessMode::Read);
            if (!slot.staging) {
                throw std::runtime_error("NVRHI failed to create a picking staging texture.");
            }
        }
    }

    m_impl->ensureTargets(device, context.targets());

    const auto& framebuffer_info = m_impl->framebuffer->getFramebufferInfo();
    if (!m_impl->pipeline || m_impl->pipeline_framebuffer_info !=
                                     static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        // 深度测试开、深度写关：GBuffer 阶段已经把深度写好了，这里只借它判可见性。
        // LessOrEqual 而不是 Less —— 相等才是「就是屏幕上那个片元」。
        nvrhi::DepthStencilState depth_state;
        depth_state.setDepthTestEnable(true)
                .setDepthWriteEnable(false)
                .setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual)
                .disableStencil();
        nvrhi::RasterState raster_state;
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
            throw std::runtime_error("NVRHI failed to create the picking graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the picking graphics pipeline");
    }
}

void PickingPass::record(PassRecordContext& context) {
    if (!m_impl->pipeline || m_impl->slots.empty()) {
        throw std::logic_error("PickingPass was not prepared.");
    }

    auto& slot = m_impl->slots.at(context.frameSlotIndex());
    auto& device = context.device();

    // 先收上一轮的结果。走到这个 slot 说明它上次提交的活已经完成（frame slot 的语义就是
    // 「这一格的 GPU 活已经做完了」），所以这里 map 不会 stall。
    if (slot.pending) {
        size_t row_pitch = 0;
        const auto slice = nvrhi::TextureSlice{}.setSize(1, 1, 1);
        const auto* mapped = static_cast<const uint32_t*>(device.mapStagingTexture(slot.staging,
                slice, nvrhi::CpuAccessMode::Read, &row_pitch));
        if (mapped == nullptr) {
            slot.pending = false;
            throw std::runtime_error("NVRHI failed to map the picking staging texture.");
        }
        uint32_t picking_id = 0;
        if (row_pitch >= sizeof(uint32_t)) {
            std::memcpy(&picking_id, mapped, sizeof(picking_id));
        }
        device.unmapStagingTexture(slot.staging);
        slot.pending = false;

        // 后到的结果覆盖先到的：连续点击时用户要的是最后一次。
        m_impl->ready_result = PickResult{ picking_id, slot.x, slot.y };
    }

    const auto& request = context.frame().settings().pick;
    if (!request.has_value()) {
        return;
    }

    auto& commands = context.commands();
    auto& framebuffer = *m_impl->framebuffer;
    const auto& framebuffer_info = framebuffer.getFramebufferInfo();

    // 0 是「空处」，所以先整张清成 0。
    commands.clearTextureUInt(m_impl->id_texture, nvrhi::AllSubresources, 0);

    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer_info.getViewport());

    const auto& scene = context.frame().scene();
    const glm::mat4 view_projection = scene.view.projection * scene.view.view;

    for (const auto& draw: scene.draws) {
        // picking_id 为 0 的 draw 不参与拾取 —— 那个值是「空处」的保留编号。
        if (draw.picking_id == 0) {
            continue;
        }
        const auto resolved = detail::resolveDraw(context.frame(), draw);
        if (!resolved) {
            continue;
        }

        PickingConstants constants{};
        const glm::mat4 mvp = view_projection * draw.transform;
        std::memcpy(constants.model_view_projection.data(), glm::value_ptr(mvp),
                sizeof(constants.model_view_projection));
        constants.picking_id = draw.picking_id;

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

    // 请求的像素夹到目标范围内。面板尺寸和 ID 缓冲尺寸之间有一帧延迟（ImGui 的布局是
    // 上一帧量的），拖动时可能短暂越界 —— 越界的 copyTexture 是未定义行为。
    const uint32_t max_x = framebuffer_info.width == 0 ? 0 : framebuffer_info.width - 1;
    const uint32_t max_y = framebuffer_info.height == 0 ? 0 : framebuffer_info.height - 1;
    const uint32_t pixel_x = std::min(request->x, max_x);
    const uint32_t pixel_y = std::min(request->y, max_y);

    commands.copyTexture(slot.staging, nvrhi::TextureSlice{}.setSize(1, 1, 1), m_impl->id_texture,
            nvrhi::TextureSlice{}.setOrigin(pixel_x, pixel_y).setSize(1, 1, 1));
    slot.pending = true;
    slot.x = pixel_x;
    slot.y = pixel_y;
}

std::optional<PickResult> PickingPass::takeResult() noexcept {
    auto result = m_impl->ready_result;
    m_impl->ready_result.reset();
    return result;
}

} // namespace arti::rendering
