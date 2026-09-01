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

// MVP 在 CPU 侧乘好而不是像 GBufferPass 那样分开传 —— 拾取不需要世界坐标，所以没必要为它
// 建一个逐帧 UBO；而且 model + view_projection + id 是 132 字节，越过了 Vulkan 保证的 128。
//
// 因此这里算出的深度和 GBufferPass 的不逐位相同。这个 pass 用自己的深度缓冲就是为了不受它
// 影响，见 Impl::depth_texture。
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
constexpr auto pickingDepthFormat = nvrhi::Format::D32;

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

    // ID 缓冲、这个 pass 自己的深度缓冲，和把两者绑起来的 framebuffer。
    //
    // 深度**不能**借 GBuffer 写好的那张再用 LessOrEqual 去比：那等于要求两个 pass 算出的
    // SV_Position.z 逐位相同，而这里的 MVP 是 CPU 侧乘好的一个矩阵、GBufferPass 是在着色器里
    // 分两步乘的（mul(view_projection, mul(model, p))）。两者数学上相等、浮点上不是 ——
    // 差一个 ULP 那个片元就测试失败、ID 写不进去。误差的符号逐三角形/逐像素乱跳，所以表现成
    // 「同一个物体，这里点得到、那里点不到，动一下相机结论又变了」。
    //
    // 自己清一张深度、自己写，可见性就由这个 pass 自洽地决定，不再依赖跨 pass 的位级一致。
    // 几何本来就要重画一遍，多写一次深度不额外花钱；代价只是一张和场景同尺寸的 D32。
    nvrhi::TextureHandle id_texture;
    nvrhi::TextureHandle depth_texture;
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

        nvrhi::TextureDesc depth_desc;
        depth_desc.setWidth(scene_info.width)
                .setHeight(scene_info.height)
                .setFormat(pickingDepthFormat)
                .setIsRenderTarget(true)
                .setDebugName("ArtiRenderer PickingDepth")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::DepthWrite);
        depth_texture = device.createTexture(depth_desc);
        if (!depth_texture) {
            throw std::runtime_error("NVRHI failed to create the picking depth texture.");
        }

        nvrhi::FramebufferDesc framebuffer_desc;
        framebuffer_desc.addColorAttachment(id_texture).setDepthAttachment(depth_texture.Get());
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
        // 深度是这个 pass 自己的，所以测试和写入都开、Less —— 和 GBufferPass 同一个约定。
        // 为什么不借 GBuffer 的深度做 LessOrEqual，见 Impl 里 depth_texture 上面那段。
        nvrhi::DepthStencilState depth_state;
        depth_state.setDepthTestEnable(true)
                .setDepthWriteEnable(true)
                .setDepthFunc(nvrhi::ComparisonFunc::Less)
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

    // 0 是「空处」，所以 ID 缓冲先整张清成 0；深度清成 1.0（远平面）。
    // 清在这里而不是 ClearScenePass：这两张只在有拾取请求的帧才有意义。
    commands.clearTextureUInt(m_impl->id_texture, nvrhi::AllSubresources, 0);
    commands.clearDepthStencilTexture(m_impl->depth_texture, nvrhi::AllSubresources, true, 1.0f,
            false, 0);

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
        // 和 GBufferPass 逐条对齐地跳过：那边不画的东西屏幕上就不存在，这边要是画了，
        // 点空处会选中一个看不见的实体。加几何 pass（透明、蒙皮）时这个条件要跟着放宽。
        if (!resolved || resolved->material.type != MaterialType::PBR) {
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
