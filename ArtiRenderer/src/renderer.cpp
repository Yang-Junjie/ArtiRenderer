#include "renderer.h"

#include "artichoco/renderer/render_device.h"
#include "detail/format_mapping.h"
#include "detail/log.h"
#include "detail/resource_registry.h"
#include "pipeline/frame_context.h"
#include "pipeline/passes/picking_pass.h"
#include "pipeline/pipeline.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace arti::rendering {


namespace {

std::unique_ptr<Pipeline> createPipeline(PipelineKind kind, arti::renderer::RenderDevice& device) {
    switch (kind) {
        case PipelineKind::Deferred:
            return createDeferredPipeline(device);
    }
    throw std::invalid_argument("Unsupported ArtiRenderer pipeline kind.");
}

} // namespace

struct Renderer::Impl {
    Impl(arti::renderer::RenderDevice& device, const RendererCreateInfo& info)
            : device(&device),
              resources(device),
              pipeline(createPipeline(info.pipeline, device)) {
        settings.present = info.present;
        // 随机 64 位 UUID，所以不会跟任何真实纹理的句柄撞上。离屏目标由 RenderTargetSet 拥有，
        // 不进 ResourceRegistry —— 这个 id 只是让 ImGuiPass 认出「要的是那张离屏目标」，
        // 实际解析到的是 tone mapping 之后的 DisplayColor。
        settings.scene_color_id = TextureHandle::generate();
    }

    arti::renderer::RenderDevice* device{ nullptr };
    detail::ResourceRegistry resources;
    std::unique_ptr<Pipeline> pipeline;
    FrameSettings settings;
    // 调试线在这里攒着，renderFrame() 把它快照进 FrameSettings 再清空。容量留着复用。
    std::vector<DebugLine> debug_lines;
};

Renderer::Renderer(arti::renderer::RenderDevice& device, const RendererCreateInfo& info)
        : m_impl(std::make_unique<Impl>(device, info)) {
    getLogChannel().info("ArtiRenderer ready (pipeline: {})", m_impl->pipeline->name());
}

Renderer::~Renderer() = default;

MeshHandle Renderer::createMesh(const Mesh& mesh, std::string_view debug_name) {
    return m_impl->resources.createMesh(mesh, debug_name);
}

TextureHandle Renderer::createTexture(const TextureDesc& desc) {
    return m_impl->resources.createTexture(desc);
}

MaterialHandle Renderer::createMaterial(const Material& material) {
    return m_impl->resources.createMaterial(material);
}

bool Renderer::updateMaterial(MaterialHandle handle, const Material& material) {
    return m_impl->resources.updateMaterial(handle, material);
}

bool Renderer::destroyMesh(MeshHandle handle) { return m_impl->resources.destroyMesh(handle); }

bool Renderer::destroyTexture(TextureHandle handle) {
    return m_impl->resources.destroyTexture(handle);
}

bool Renderer::destroyMaterial(MaterialHandle handle) {
    return m_impl->resources.destroyMaterial(handle);
}

std::optional<TextureInfo> Renderer::textureInfo(TextureHandle handle) const {
    const auto* texture = m_impl->resources.findTexture(handle);
    if (texture == nullptr) {
        return std::nullopt;
    }

    TextureInfo info;
    info.width = texture->width();
    info.height = texture->height();
    info.mip_levels = texture->mipLevels();
    info.format = detail::fromRHIFormat(texture->format());
    info.built_in = handle == m_impl->resources.whiteTexture() ||
                    handle == m_impl->resources.flatNormalTexture();
    return info;
}

std::optional<Material> Renderer::material(MaterialHandle handle) const {
    const auto* found = m_impl->resources.findMaterial(handle);
    return found == nullptr ? std::nullopt : std::optional<Material>{ *found };
}

std::optional<MeshInfo> Renderer::meshInfo(MeshHandle handle) const {
    const auto* mesh = m_impl->resources.findMesh(handle);
    if (mesh == nullptr) {
        return std::nullopt;
    }

    MeshInfo info;
    info.bounds = mesh->bounds;
    info.submesh_count = static_cast<uint32_t>(mesh->submeshes.size());
    info.vertex_count = mesh->vertex_buffer.vertexCount();
    info.index_count = mesh->index_buffer.indexCount();
    return info;
}

TextureHandle Renderer::whiteTexture() const noexcept { return m_impl->resources.whiteTexture(); }

TextureHandle Renderer::flatNormalTexture() const noexcept {
    return m_impl->resources.flatNormalTexture();
}

FrameStatistics Renderer::renderFrame(const RenderScene& scene, const FrameOverlay& overlay) {
    const auto output = outputInfo();
    if (!output.available) {
        return FrameStatistics{};
    }

    // 借出去而不是拷一份：FrameContext 的生命周期在这个函数里，pass 用完就还。
    m_impl->settings.debug_lines = m_impl->debug_lines;

    FrameContext frame{ scene, overlay, m_impl->resources, output, m_impl->settings };
    m_impl->settings.pick.reset();

    m_impl->pipeline->render(frame);

    // 调试线只作用于这一帧。清空放在 render() 之后而不是之前 —— pass 是在 render() 里面读的。
    m_impl->settings.debug_lines = {};
    m_impl->debug_lines.clear();
    return frame.statistics();
}

void Renderer::setPresentMode(PresentMode mode) noexcept { m_impl->settings.present = mode; }

PresentMode Renderer::presentMode() const noexcept { return m_impl->settings.present; }

void Renderer::setSceneTargetSize(uint32_t width, uint32_t height) noexcept {
    m_impl->settings.scene_target_width = width;
    m_impl->settings.scene_target_height = height;
}

uint64_t Renderer::sceneColorTextureId() const noexcept {
    return imguiTextureId(m_impl->settings.scene_color_id);
}

void Renderer::drawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color) {
    m_impl->debug_lines.push_back(DebugLine{ from, to, color });
}

void Renderer::drawAABB(const AABB& bounds, const glm::vec4& color) {
    if (bounds.isEmpty()) {
        return;
    }
    // 八个角按位组合：bit0 = x, bit1 = y, bit2 = z，0 取 min、1 取 max。
    std::array<glm::vec3, 8> corners{};
    for (int index = 0; index < 8; ++index) {
        corners[static_cast<size_t>(index)] = glm::vec3{
            (index & 1) != 0 ? bounds.max.x : bounds.min.x,
            (index & 2) != 0 ? bounds.max.y : bounds.min.y,
            (index & 4) != 0 ? bounds.max.z : bounds.min.z,
        };
    }
    // 12 条边 = 只差一个 bit 的角点对。手写出来比写循环判位更好读。
    constexpr std::array<std::pair<int, int>, 12> edges{ { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } } };
    for (const auto& [a, b]: edges) {
        drawLine(corners[static_cast<size_t>(a)], corners[static_cast<size_t>(b)], color);
    }
}

void Renderer::drawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color,
        uint32_t segments) {
    // 少于 3 段连不成环，半径非正就没什么可画的。
    if (radius <= 0.0f || segments < 3) {
        return;
    }
    constexpr float kTwoPi = 6.28318530718f;
    const float step = kTwoPi / static_cast<float>(segments);
    // 三个正交大圆。不画经纬网格是刻意的：三个圈已经够看出球心和半径，
    // 而网格在场景里会糊成一团。
    for (uint32_t index = 0; index < segments; ++index) {
        const float a = static_cast<float>(index) * step;
        const float b = static_cast<float>(index + 1) * step;
        const float cos_a = std::cos(a) * radius;
        const float sin_a = std::sin(a) * radius;
        const float cos_b = std::cos(b) * radius;
        const float sin_b = std::sin(b) * radius;
        drawLine(center + glm::vec3{ cos_a, sin_a, 0.0f }, center + glm::vec3{ cos_b, sin_b, 0.0f },
                color);
        drawLine(center + glm::vec3{ cos_a, 0.0f, sin_a }, center + glm::vec3{ cos_b, 0.0f, sin_b },
                color);
        drawLine(center + glm::vec3{ 0.0f, cos_a, sin_a }, center + glm::vec3{ 0.0f, cos_b, sin_b },
                color);
    }
}

void Renderer::requestPick(const PickRequest& request) noexcept { m_impl->settings.pick = request; }

std::optional<PickResult> Renderer::takePickResult() noexcept {
    auto* picking = m_impl->pipeline->pickingPass();
    return picking == nullptr ? std::nullopt : picking->takeResult();
}

RenderOutputInfo Renderer::outputInfo() const noexcept {
    const auto swapchain = m_impl->device->swapchainInfo();
    RenderOutputInfo info;
    info.width = swapchain.width;
    info.height = swapchain.height;
    info.kind = RenderTargetKind::Swapchain;
    info.available = swapchain.available;
    return info;
}

void Renderer::waitIdle() const { m_impl->device->waitIdle(); }

} // namespace arti::rendering
