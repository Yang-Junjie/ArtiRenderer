#include "renderer.h"

#include "artichoco/renderer/render_device.h"
#include "detail/format_mapping.h"
#include "detail/log.h"
#include "detail/resource_registry.h"
#include "pipeline/frame_context.h"
#include "pipeline/pipeline.h"

#include <stdexcept>
#include <utility>

namespace arti::rendering {


namespace {

std::unique_ptr<Pipeline> createPipeline(PipelineKind kind, arti::renderer::RenderDevice& device) {
    switch (kind) {
        case PipelineKind::Forward:
            return createForwardPipeline(device);
    }
    throw std::invalid_argument("Unsupported ArtiRenderer pipeline kind.");
}

} // namespace

struct Renderer::Impl {
    Impl(arti::renderer::RenderDevice& device, const RendererCreateInfo& info)
            : device(&device),
              resources(device),
              pipeline(createPipeline(info.pipeline, device)) {}

    arti::renderer::RenderDevice* device{ nullptr };
    detail::ResourceRegistry resources;
    std::unique_ptr<Pipeline> pipeline;
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

TextureHandle Renderer::whiteTexture() const noexcept { return m_impl->resources.whiteTexture(); }

TextureHandle Renderer::flatNormalTexture() const noexcept {
    return m_impl->resources.flatNormalTexture();
}

FrameStatistics Renderer::renderFrame(const RenderScene& scene) {
    const auto output = outputInfo();
    if (!output.available) {
        // 窗口最小化 / swapchain 待重建，这一帧整体跳过。
        return FrameStatistics{};
    }

    FrameContext frame{ scene, m_impl->resources, output };
    m_impl->pipeline->render(frame);
    return frame.statistics();
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
