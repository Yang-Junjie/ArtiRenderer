#include "imgui_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "frame_overlay.h"
#include "log.h"
#include "shader_paths.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace arti::rendering {
namespace {

// scale/translate 就够把 ImGui 的像素坐标变成 NDC，不需要矩阵。
//
// decode_srgb 逐 draw 变：ImGui 自己的纹理（字体图集）存的是 sRGB 数值要解码，而 SceneColor 是
// 线性的离屏目标，再解一次就偏暗。用一个 flag 而不是两条管线 —— 一个 PSO 就够。
struct GuiPushConstants {
    std::array<float, 2> scale;
    std::array<float, 2> translate;
    uint32_t decode_srgb;
    // Slang 把 push constant 块的大小向上取整到最大成员的对齐（float2 → 8），所以它那边这个结构
    // 是 24 字节而不是 20。C++ 这边 uint32_t 只要求 4 字节对齐，不补齐的话 setPushConstants
    // 会因为大小不匹配被 NVRHI 拒掉。显式补而不是 alignas：这样这 4 个字节是零，
    // 不会把未初始化的内存送给 GPU。
    uint32_t padding{ 0 };
};

static_assert(std::is_standard_layout_v<GuiPushConstants>);
static_assert(sizeof(GuiPushConstants) == 24);
static_assert(offsetof(GuiPushConstants, decode_srgb) == 16);

// 翻倍增长，避免窗口一多就每帧重建缓冲。64 KB 起步够画几千个顶点。
size_t growCapacity(size_t current, size_t required) {
    constexpr size_t minimum_capacity = 64 * 1'024;
    size_t capacity = std::max(current, minimum_capacity);
    while (capacity < required) {
        if (capacity > std::numeric_limits<size_t>::max() / 2) {
            return required;
        }
        capacity *= 2;
    }
    return capacity;
}

constexpr nvrhi::Format imguiIndexFormat() {
    static_assert(sizeof(ImDrawIdx) == sizeof(uint16_t) || sizeof(ImDrawIdx) == sizeof(uint32_t),
            "Dear ImGui indices must be 16-bit or 32-bit.");
    return sizeof(ImDrawIdx) == sizeof(uint16_t) ? nvrhi::Format::R16_UINT
                                                 : nvrhi::Format::R32_UINT;
}

// imguiTextureId() 的逆运算。两边都是 uint64_t，所以只是换个类型壳子。
TextureHandle toTextureHandle(ImTextureID id) noexcept {
    return TextureHandle{ core::UUID{ static_cast<core::UUID::Value>(id) } };
}

bool hasDrawData(const ImDrawData* draw_data) noexcept {
    return draw_data != nullptr && draw_data->Valid && draw_data->TotalVtxCount > 0 &&
           draw_data->TotalIdxCount > 0 && draw_data->DisplaySize.x > 0.0f &&
           draw_data->DisplaySize.y > 0.0f;
}

} // namespace

struct ImGuiPass::Impl {
    // 每个飞行帧一份：GPU 可能还在读上一帧的几何，不能就地覆盖。
    struct FrameSlotBuffers {
        nvrhi::BufferHandle vertices;
        nvrhi::BufferHandle indices;
        size_t vertex_capacity{ 0 };
        size_t index_capacity{ 0 };
    };

    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::InputLayoutHandle input_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    // PSO 只依赖格式和采样数，窗口缩放不用重建。
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // TextureHandle -> binding set。注册表里的 UI 纹理不是渲染目标，不会因为 backbuffer 换了而
    // 失效，所以可以跨帧缓存 —— 和 UnlitOpaquePass 同一个套路。
    std::unordered_map<TextureHandle, nvrhi::BindingSetHandle> binding_sets;

    // SceneColor 单独存：它是渲染目标，缩放时会重建，所以要跟着 RenderTargetSet 的 revision 失效。
    // 混进上面那张表里就会在缩放后绑到已经作废的纹理上。
    nvrhi::BindingSetHandle scene_color_set;
    uint64_t scene_color_revision{ std::numeric_limits<uint64_t>::max() };

    std::vector<FrameSlotBuffers> frame_slots;
    // ImDrawData 是一串 draw list，这里拼成一对连续缓冲再一次上传。
    std::vector<ImDrawVert> vertices;
    std::vector<ImDrawIdx> indices;

    void ensureBuffers(nvrhi::IDevice& device, FrameSlotBuffers& slot, size_t vertex_bytes,
            size_t index_bytes) {
        if (vertex_bytes > slot.vertex_capacity) {
            slot.vertex_capacity = growCapacity(slot.vertex_capacity, vertex_bytes);
            nvrhi::BufferDesc desc;
            desc.setByteSize(slot.vertex_capacity)
                    .setIsVertexBuffer(true)
                    .setDebugName("ArtiRenderer ImGui vertices")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::CopyDest);
            slot.vertices = device.createBuffer(desc);
            if (!slot.vertices) {
                throw std::runtime_error("NVRHI failed to create the ImGui vertex buffer.");
            }
        }

        if (index_bytes > slot.index_capacity) {
            slot.index_capacity = growCapacity(slot.index_capacity, index_bytes);
            nvrhi::BufferDesc desc;
            desc.setByteSize(slot.index_capacity)
                    .setIsIndexBuffer(true)
                    .setDebugName("ArtiRenderer ImGui indices")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::CopyDest);
            slot.indices = device.createBuffer(desc);
            if (!slot.indices) {
                throw std::runtime_error("NVRHI failed to create the ImGui index buffer.");
            }
        }
    }

    nvrhi::BindingSetHandle makeBindingSet(nvrhi::IDevice& device, nvrhi::ITexture& texture) {
        const std::array resources = {
            arti::renderer::vulkan::NvrhiBindingResource::Texture("gui_texture", texture),
            arti::renderer::vulkan::NvrhiBindingResource::Sampler("gui_sampler", *sampler),
        };
        auto binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device, reflection, 0,
                *binding_layout, resources);
        if (!binding_set) {
            throw std::runtime_error("NVRHI failed to create an ImGui binding set.");
        }
        return binding_set;
    }

    // 一次 draw 需要的东西：绑定集，以及采样出来的数据要不要从 sRGB 解码。
    struct Resolved {
        nvrhi::IBindingSet* binding_set{ nullptr };
        bool decode_srgb{ true };
    };

    Resolved resolve(PassRecordContext& context, ImTextureID id) {
        const auto handle = toTextureHandle(id);

        // SceneColor：由 RenderTargetSet 拥有，不在注册表里，所以走单独这条路。
        // 数据是线性的（RGBA8_UNORM 离屏目标），不解码。
        if (handle == context.frame().settings().scene_color_id) {
            auto& targets = context.targets();
            if (!scene_color_set || scene_color_revision != targets.revision()) {
                scene_color_set = makeBindingSet(context.device(), targets.sceneColor());
                scene_color_revision = targets.revision();
            }
            return { scene_color_set, false };
        }

        const auto cached = binding_sets.find(handle);
        if (cached != binding_sets.end()) {
            return { cached->second, true };
        }

        // 这里不走 resolveTexture 的白图兜底：UI 要的纹理不在注册表里说明宿主漏了一步
        // （最典型的是字体图集没建、或者建完没 SetTexID），静默画成白块只会更难查。
        if (context.frame().resources().findTexture(handle) == nullptr) {
            throw std::runtime_error("Dear ImGui asked for texture id " +
                                     std::to_string(static_cast<uint64_t>(id)) +
                                     ", which is not a live ArtiRenderer texture.");
        }

        auto binding_set = makeBindingSet(context.device(), context.texture(handle));
        return { binding_sets.emplace(handle, std::move(binding_set)).first->second, true };
    }
};

ImGuiPass::ImGuiPass()
        : m_impl(std::make_unique<Impl>()) {}

ImGuiPass::~ImGuiPass() = default;

bool ImGuiPass::isEnabled(const FrameContext& frame) const {
    // 没 UI 的一帧连 prepare 都不进，shader 也就不会编译 —— 不用 UI 的运行时零成本。
    //
    // IntoUI 模式例外：那时 PresentPass 是关的，没人写 backbuffer，所以即使这一帧没有 draw data
    // 也得进来把它清掉，否则屏上是上一帧的残留或者未初始化的内容。
    return hasDrawData(frame.overlay().imgui_draw_data) ||
           frame.settings().present == PresentMode::IntoUI;
}

void ImGuiPass::prepare(PassPrepareContext& context) {
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("imgui.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(device, program,
                "ArtiRenderer ImGui");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The ImGui shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        // 顶点布局直接照 ImDrawVert 的内存布局来，不做转换。
        nvrhi::VertexAttributeDesc position;
        position.setName("POSITION")
                .setFormat(nvrhi::Format::RG32_FLOAT)
                .setBufferIndex(0)
                .setOffset(offsetof(ImDrawVert, pos))
                .setElementStride(sizeof(ImDrawVert));
        nvrhi::VertexAttributeDesc uv;
        uv.setName("TEXCOORD0")
                .setFormat(nvrhi::Format::RG32_FLOAT)
                .setBufferIndex(0)
                .setOffset(offsetof(ImDrawVert, uv))
                .setElementStride(sizeof(ImDrawVert));
        nvrhi::VertexAttributeDesc color;
        color.setName("COLOR0")
                .setFormat(nvrhi::Format::RGBA8_UNORM)
                .setBufferIndex(0)
                .setOffset(offsetof(ImDrawVert, col))
                .setElementStride(sizeof(ImDrawVert));
        const std::array attributes = { position, uv, color };
        m_impl->input_layout = device.createInputLayout(attributes.data(),
                static_cast<uint32_t>(attributes.size()), m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the ImGui input layout.");
        }

        nvrhi::SamplerDesc sampler_desc;
        sampler_desc.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge);
        m_impl->sampler = device.createSampler(sampler_desc);
        if (!m_impl->sampler) {
            throw std::runtime_error("NVRHI failed to create the ImGui sampler.");
        }

        m_impl->frame_slots.resize(context.frameSlotCount());
    }

    // 画的是 backbuffer，不是 SceneColor —— 这个 pass 排在 PresentPass 之后。
    const auto& framebuffer_info = context.targets().outputFramebuffer().getFramebufferInfo();
    if (!m_impl->pipeline || m_impl->pipeline_framebuffer_info !=
                                     static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        // 标准的 UI 混合：src * srcAlpha + dst * (1 - srcAlpha)。
        nvrhi::BlendState::RenderTarget blend_target;
        blend_target.enableBlend()
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setBlendOp(nvrhi::BlendOp::Add)
                .setSrcBlendAlpha(nvrhi::BlendFactor::One)
                .setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha)
                .setBlendOpAlpha(nvrhi::BlendOp::Add)
                .setColorWriteMask(nvrhi::ColorMask::All);
        nvrhi::BlendState blend_state;
        blend_state.setRenderTarget(0, blend_target);
        // backbuffer 没有深度附件，UI 也不需要深度。scissor 必须开：ImGui 靠它裁剪窗口。
        nvrhi::DepthStencilState depth_state;
        depth_state.disableDepthTest().disableDepthWrite().disableStencil();
        nvrhi::RasterState raster_state;
        raster_state.setCullNone().setScissorEnable(true);
        nvrhi::RenderState render_state;
        render_state.setBlendState(blend_state)
                .setDepthStencilState(depth_state)
                .setRasterState(raster_state);

        nvrhi::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setInputLayout(m_impl->input_layout)
                .setVertexShader(m_impl->vertex_shader)
                .setPixelShader(m_impl->pixel_shader)
                .setRenderState(render_state)
                .addBindingLayout(m_impl->binding_layout);
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the ImGui graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the ImGui graphics pipeline");
    }
}

void ImGuiPass::record(PassRecordContext& context) {
    if (!m_impl->pipeline) {
        throw std::logic_error("ImGuiPass was not prepared.");
    }

    // IntoUI 模式下 UI 独占 backbuffer，得自己清 —— PresentPass 关着，没人写它。
    // Direct 模式下不清：场景已经在里面了，清了就把它擦掉。
    if (context.frame().settings().present == PresentMode::IntoUI) {
        context.commands().clearTextureFloat(&context.outputColor(), nvrhi::AllSubresources,
                nvrhi::Color{ 0.0f, 0.0f, 0.0f, 1.0f });
    }

    // IntoUI 模式下 isEnabled() 即使没有 draw data 也放行（为了上面那次清屏），所以这里要再判一次。
    const ImDrawData* draw_data = context.frame().overlay().imgui_draw_data;
    if (!hasDrawData(draw_data)) {
        return;
    }

    // 所有 draw list 拼成一对连续缓冲，一次上传。draw 时用 base offset 定位回各自的段。
    m_impl->vertices.clear();
    m_impl->indices.clear();
    m_impl->vertices.reserve(static_cast<size_t>(draw_data->TotalVtxCount));
    m_impl->indices.reserve(static_cast<size_t>(draw_data->TotalIdxCount));
    for (const ImDrawList* draw_list: draw_data->CmdLists) {
        m_impl->vertices.insert(m_impl->vertices.end(), draw_list->VtxBuffer.Data,
                draw_list->VtxBuffer.Data + draw_list->VtxBuffer.Size);
        m_impl->indices.insert(m_impl->indices.end(), draw_list->IdxBuffer.Data,
                draw_list->IdxBuffer.Data + draw_list->IdxBuffer.Size);
    }

    auto& slot = m_impl->frame_slots.at(context.frameSlotIndex());
    const size_t vertex_bytes = m_impl->vertices.size() * sizeof(ImDrawVert);
    const size_t index_bytes = m_impl->indices.size() * sizeof(ImDrawIdx);
    m_impl->ensureBuffers(context.device(), slot, vertex_bytes, index_bytes);

    auto& commands = context.commands();
    commands.writeBuffer(slot.vertices, m_impl->vertices.data(), vertex_bytes);
    commands.writeBuffer(slot.indices, m_impl->indices.data(), index_bytes);

    // ImGui 给的是像素坐标（原点左上），这里线性映射到 NDC。Y 取负是因为 NDC 的 Y 朝上。
    // decode_srgb 逐 draw 覆盖，见下面的循环。
    GuiPushConstants constants{
        { 2.0f / draw_data->DisplaySize.x, -2.0f / draw_data->DisplaySize.y },
        {
            -1.0f - draw_data->DisplayPos.x * (2.0f / draw_data->DisplaySize.x),
            1.0f - draw_data->DisplayPos.y * (-2.0f / draw_data->DisplaySize.y),
        },
        1,
        0,
    };

    auto& framebuffer = context.targets().outputFramebuffer();
    const auto& framebuffer_info = framebuffer.getFramebufferInfo();
    const auto framebuffer_width = static_cast<float>(framebuffer_info.width);
    const auto framebuffer_height = static_cast<float>(framebuffer_info.height);
    const ImVec2 clip_offset = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;

    size_t base_vertex = 0;
    size_t base_index = 0;
    for (const ImDrawList* draw_list: draw_data->CmdLists) {
        for (const ImDrawCmd& command: draw_list->CmdBuffer) {
            // 宿主注册的自定义回调。ResetRenderState 那个哨兵值不是真回调，跳过即可 ——
            // 我们每条 draw 都重设完整 GraphicsState，本来就没有需要复位的残留状态。
            if (command.UserCallback != nullptr) {
                if (command.UserCallback != ImGui::GetPlatformIO().DrawCallback_ResetRenderState) {
                    command.UserCallback(draw_list, &command);
                }
                continue;
            }

            // 裁剪矩形从 ImGui 的显示坐标换算到 framebuffer 像素，再夹到 framebuffer 内 ——
            // 越界的 scissor 在 Vulkan 里是未定义行为。
            const float min_x = std::clamp((command.ClipRect.x - clip_offset.x) * clip_scale.x,
                    0.0f, framebuffer_width);
            const float min_y = std::clamp((command.ClipRect.y - clip_offset.y) * clip_scale.y,
                    0.0f, framebuffer_height);
            const float max_x = std::clamp((command.ClipRect.z - clip_offset.x) * clip_scale.x,
                    0.0f, framebuffer_width);
            const float max_y = std::clamp((command.ClipRect.w - clip_offset.y) * clip_scale.y,
                    0.0f, framebuffer_height);
            // 完全被裁掉的 draw 直接丢，省一次状态切换。
            if (max_x <= min_x || max_y <= min_y) {
                continue;
            }

            nvrhi::ViewportState viewport;
            viewport.addViewport(framebuffer_info.getViewport())
                    .addScissorRect(nvrhi::Rect{
                        static_cast<int>(min_x),
                        static_cast<int>(max_x),
                        static_cast<int>(min_y),
                        static_cast<int>(max_y),
                    });

            const auto resolved = m_impl->resolve(context, command.GetTexID());
            constants.decode_srgb = resolved.decode_srgb ? 1u : 0u;

            nvrhi::GraphicsState state;
            state.setPipeline(m_impl->pipeline)
                    .setFramebuffer(&framebuffer)
                    .setViewport(viewport)
                    .addBindingSet(resolved.binding_set)
                    .addVertexBuffer(
                            nvrhi::VertexBufferBinding().setBuffer(slot.vertices).setSlot(0))
                    .setIndexBuffer(nvrhi::IndexBufferBinding()
                                    .setBuffer(slot.indices)
                                    .setFormat(imguiIndexFormat()));
            commands.setGraphicsState(state);
            // push constants 必须在 setGraphicsState 之后：换状态会让它失效。
            commands.setPushConstants(&constants, sizeof(constants));
            commands.drawIndexed(nvrhi::DrawArguments{}
                            .setVertexCount(command.ElemCount)
                            .setStartIndexLocation(
                                    static_cast<uint32_t>(base_index + command.IdxOffset))
                            .setStartVertexLocation(
                                    static_cast<uint32_t>(base_vertex + command.VtxOffset)));
        }
        base_index += static_cast<size_t>(draw_list->IdxBuffer.Size);
        base_vertex += static_cast<size_t>(draw_list->VtxBuffer.Size);
    }
}

} // namespace arti::rendering
