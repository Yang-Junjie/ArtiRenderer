#include "linear_pass.h"

#include <stdexcept>
#include <string>

namespace arti::rendering {
namespace {

const detail::GPUMesh& requireMesh(const FrameContext& frame, MeshHandle handle) {
    if (const auto* mesh = frame.resources().findMesh(handle)) {
        return *mesh;
    }
    throw std::runtime_error("Unknown mesh handle " + handle.toString() + " in the draw list.");
}

} // namespace

PassPrepareContext::PassPrepareContext(arti::renderer::RenderPassPrepareContext& rhi,
        FrameContext& frame, RenderTargetSet& targets, GBufferTargets& gbuffer,
        EnvironmentResources& environment) noexcept
        : m_rhi(&rhi),
          m_frame(&frame),
          m_targets(&targets),
          m_gbuffer(&gbuffer),
          m_environment(&environment) {}

nvrhi::IDevice& PassPrepareContext::device() const noexcept { return m_rhi->device(); }

size_t PassPrepareContext::frameSlotCount() const noexcept { return m_rhi->frameSlotCount(); }

FrameContext& PassPrepareContext::frame() const noexcept { return *m_frame; }

RenderTargetSet& PassPrepareContext::targets() const noexcept { return *m_targets; }

GBufferTargets& PassPrepareContext::gbuffer() const noexcept { return *m_gbuffer; }

EnvironmentResources& PassPrepareContext::environment() const noexcept { return *m_environment; }

PassRecordContext::PassRecordContext(arti::renderer::RenderPassContext& rhi, FrameContext& frame,
        RenderTargetSet& targets, GBufferTargets& gbuffer,
        EnvironmentResources& environment) noexcept
        : m_rhi(&rhi),
          m_frame(&frame),
          m_targets(&targets),
          m_gbuffer(&gbuffer),
          m_environment(&environment) {}

nvrhi::IDevice& PassRecordContext::device() const noexcept { return m_rhi->device(); }

nvrhi::ICommandList& PassRecordContext::commands() const noexcept { return m_rhi->commands(); }

size_t PassRecordContext::frameSlotIndex() const noexcept { return m_rhi->frameSlotIndex(); }

nvrhi::ITexture& PassRecordContext::outputColor() const noexcept { return m_rhi->colorTexture(); }

FrameContext& PassRecordContext::frame() const noexcept { return *m_frame; }

RenderTargetSet& PassRecordContext::targets() const noexcept { return *m_targets; }

GBufferTargets& PassRecordContext::gbuffer() const noexcept { return *m_gbuffer; }

EnvironmentResources& PassRecordContext::environment() const noexcept { return *m_environment; }

nvrhi::IBuffer& PassRecordContext::vertexBuffer(MeshHandle mesh) const {
    return m_rhi->buffer(requireMesh(*m_frame, mesh).vertex_buffer);
}

nvrhi::IBuffer& PassRecordContext::indexBuffer(MeshHandle mesh) const {
    return m_rhi->buffer(requireMesh(*m_frame, mesh).index_buffer);
}

nvrhi::ITexture& PassRecordContext::texture(TextureHandle handle) const {
    return m_rhi->texture(m_frame->resources().resolveTexture(handle));
}

} // namespace arti::rendering
