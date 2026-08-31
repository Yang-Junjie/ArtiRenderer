#include "debug_line_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "log.h"
#include "shader_paths.h"

#include <array>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace arti::rendering {
namespace {

// push constant，只有一个矩阵。64 字节，稳稳在 Vulkan 保证的 128 以内。
struct DebugLineConstants {
    std::array<float, 16> view_projection;
};

static_assert(std::is_standard_layout_v<DebugLineConstants>);
static_assert(sizeof(DebugLineConstants) == sizeof(float) * 16);
static_assert(sizeof(DebugLineConstants) <= 128, "Vulkan only guarantees 128 push constant bytes.");

// 一个顶点。和 debug_line.slang 的 VertexInput 逐字段对齐。
struct DebugVertex {
    std::array<float, 3> position;
    std::array<float, 4> color;
};

static_assert(std::is_standard_layout_v<DebugVertex>);
static_assert(sizeof(DebugVertex) == sizeof(float) * 7);

// 顶点缓冲的起始容量（字节）。够放 64 条线 —— 一个包围盒 12 条、一个线框球 72 条，
// 所以这个起点大致等于「选中一个物体」的量级，超了就翻倍。
constexpr size_t kInitialVertexBytes = 64 * 2 * sizeof(DebugVertex);

size_t growCapacity(size_t current, size_t needed) {
    size_t capacity = current > 0 ? current : kInitialVertexBytes;
    while (capacity < needed) {
        capacity *= 2;
    }
    return capacity;
}

} // namespace

struct DebugLinePass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::InputLayoutHandle input_layout;

    nvrhi::GraphicsPipelineHandle pipeline;
    // PSO 只依赖格式和采样数，尺寸变了不用重建。
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // 只有 push constant，没有纹理也没有 UBO，所以绑定集是空的 —— 但仍然要有一个，
    // push constant 在 nvrhi 里是通过 binding set 里的 PushConstants 项声明的。
    nvrhi::BindingSetHandle binding_set;

    struct FrameSlot {
        nvrhi::BufferHandle vertices;
        size_t capacity{ 0 };
    };
    std::vector<FrameSlot> frame_slots;
    // 线段展开成顶点的暂存区。留成成员是为了复用容量，不用每帧分配。
    std::vector<DebugVertex> vertices;

    void ensureBuffer(nvrhi::IDevice& device, FrameSlot& slot, size_t bytes) {
        if (bytes <= slot.capacity) {
            return;
        }
        slot.capacity = growCapacity(slot.capacity, bytes);
        nvrhi::BufferDesc desc;
        desc.setByteSize(slot.capacity)
                .setIsVertexBuffer(true)
                .setDebugName("ArtiRenderer DebugLine vertices")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::CopyDest);
        slot.vertices = device.createBuffer(desc);
        if (!slot.vertices) {
            throw std::runtime_error("NVRHI failed to create the debug line vertex buffer.");
        }
    }
};

DebugLinePass::DebugLinePass()
        : m_impl(std::make_unique<Impl>()) {}

DebugLinePass::~DebugLinePass() = default;

bool DebugLinePass::isEnabled(const FrameContext& frame) const {
    return !frame.settings().debug_lines.empty();
}

void DebugLinePass::prepare(PassPrepareContext& context) {
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("debug_line.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(device, program,
                "ArtiRenderer debug line");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The debug line shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        // 属性名只影响 D3D 的语义匹配和抓帧里看到的标签 —— nvrhi 的 Vulkan 后端按数组下标
        // 分配 location，所以这里的**顺序**才是和 debug_line.slang 的契约。
        const std::array attributes = {
            nvrhi::VertexAttributeDesc()
                    .setName("POSITION")
                    .setFormat(nvrhi::Format::RGB32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(offsetof(DebugVertex, position))
                    .setElementStride(sizeof(DebugVertex)),
            nvrhi::VertexAttributeDesc()
                    .setName("COLOR0")
                    .setFormat(nvrhi::Format::RGBA32_FLOAT)
                    .setBufferIndex(0)
                    .setOffset(offsetof(DebugVertex, color))
                    .setElementStride(sizeof(DebugVertex)),
        };
        m_impl->input_layout = device.createInputLayout(attributes.data(),
                static_cast<uint32_t>(attributes.size()), m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the debug line input layout.");
        }

        // 空资源列表：这个 shader 只有 push constant。createNvrhiBindingSet 会从反射里的
        // push constant range 补出那一项，所以绑定集不是空的，只是没有别的资源要喂。
        m_impl->binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device,
                m_impl->reflection, 0, *m_impl->binding_layout, {});
        if (!m_impl->binding_set) {
            throw std::runtime_error("NVRHI failed to create the debug line binding set.");
        }
    }

    m_impl->frame_slots.resize(context.frameSlotCount());

    const auto& framebuffer_info = context.targets().displayDepthFramebuffer().getFramebufferInfo();
    if (!m_impl->pipeline || m_impl->pipeline_framebuffer_info !=
                                     static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        // 深度测试开、写关：被物体挡住的线看不见，但线之间不互相遮挡。
        nvrhi::DepthStencilState depth_state;
        depth_state.enableDepthTest().disableDepthWrite().disableStencil();
        depth_state.setDepthFunc(nvrhi::ComparisonFunc::LessOrEqual);
        // 线没有正反面可言。
        nvrhi::RasterState raster_state;
        raster_state.setCullNone();
        nvrhi::RenderState render_state;
        render_state.setDepthStencilState(depth_state).setRasterState(raster_state);

        nvrhi::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.setPrimType(nvrhi::PrimitiveType::LineList)
                .setInputLayout(m_impl->input_layout)
                .setVertexShader(m_impl->vertex_shader)
                .setPixelShader(m_impl->pixel_shader)
                .setRenderState(render_state)
                .addBindingLayout(m_impl->binding_layout);
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the debug line graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the debug line graphics pipeline");
    }
}

void DebugLinePass::record(PassRecordContext& context) {
    if (!m_impl->pipeline || !m_impl->binding_set) {
        throw std::logic_error("DebugLinePass was not prepared.");
    }

    auto& frame = context.frame();
    const auto lines = frame.settings().debug_lines;
    if (lines.empty()) {
        return;
    }

    // 一条线段两个顶点。展开成一段连续内存再一次上传 —— 逐线 writeBuffer 会让命令列表里
    // 塞进几百次小拷贝。
    m_impl->vertices.clear();
    m_impl->vertices.reserve(lines.size() * 2);
    for (const auto& line: lines) {
        DebugVertex vertex{};
        std::memcpy(vertex.color.data(), glm::value_ptr(line.color), sizeof(vertex.color));
        std::memcpy(vertex.position.data(), glm::value_ptr(line.from), sizeof(vertex.position));
        m_impl->vertices.push_back(vertex);
        std::memcpy(vertex.position.data(), glm::value_ptr(line.to), sizeof(vertex.position));
        m_impl->vertices.push_back(vertex);
    }

    auto& slot = m_impl->frame_slots.at(context.frameSlotIndex());
    const size_t bytes = m_impl->vertices.size() * sizeof(DebugVertex);
    m_impl->ensureBuffer(context.device(), slot, bytes);

    auto& commands = context.commands();
    commands.writeBuffer(slot.vertices, m_impl->vertices.data(), bytes);

    const auto& scene = frame.scene();
    DebugLineConstants constants{};
    const glm::mat4 view_projection = scene.view.projection * scene.view.view;
    std::memcpy(constants.view_projection.data(), glm::value_ptr(view_projection),
            sizeof(constants.view_projection));

    auto& framebuffer = context.targets().displayDepthFramebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    nvrhi::GraphicsState state;
    state.setPipeline(m_impl->pipeline)
            .setFramebuffer(&framebuffer)
            .setViewport(viewport)
            .addBindingSet(m_impl->binding_set)
            .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(slot.vertices).setSlot(0));
    commands.setGraphicsState(state);
    commands.setPushConstants(&constants, sizeof(constants));
    // 不计进 statistics().draw_calls：那个数字的意思是「场景里画了多少个 submesh」，
    // 把调试绘制混进去会让它失去可比性（Tonemap / Present / Sky 同理都不计）。
    commands.draw(
            nvrhi::DrawArguments{}.setVertexCount(static_cast<uint32_t>(m_impl->vertices.size())));
}

} // namespace arti::rendering
