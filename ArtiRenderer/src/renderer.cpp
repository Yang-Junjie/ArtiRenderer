#include "renderer.h"

#include "artichoco/renderer/render_device.h"
#include "detail/format_mapping.h"
#include "detail/log.h"
#include "detail/resource_registry.h"
#include "pipeline/frame_context.h"
#include "pipeline/passes/picking_pass.h"
#include "pipeline/pipeline.h"

#include <stdexcept>
#include <utility>

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

    FrameContext frame{ scene, overlay, m_impl->resources, output, m_impl->settings };
    m_impl->settings.pick.reset();

    m_impl->pipeline->render(frame);
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

void Renderer::requestPick(const PickRequest& request) noexcept {
    m_impl->settings.pick = request;
}

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
