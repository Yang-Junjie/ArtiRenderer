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
        FrameContext& frame, RenderTargetSet& targets) noexcept
        : m_rhi(&rhi),
          m_frame(&frame),
          m_targets(&targets) {}

nvrhi::IDevice& PassPrepareContext::device() const noexcept { return m_rhi->device(); }

FrameContext& PassPrepareContext::frame() const noexcept { return *m_frame; }

RenderTargetSet& PassPrepareContext::targets() const noexcept { return *m_targets; }

PassRecordContext::PassRecordContext(arti::renderer::RenderPassContext& rhi, FrameContext& frame,
        RenderTargetSet& targets) noexcept
        : m_rhi(&rhi),
          m_frame(&frame),
          m_targets(&targets) {}

nvrhi::IDevice& PassRecordContext::device() const noexcept { return m_rhi->device(); }

nvrhi::ICommandList& PassRecordContext::commands() const noexcept { return m_rhi->commands(); }

FrameContext& PassRecordContext::frame() const noexcept { return *m_frame; }

RenderTargetSet& PassRecordContext::targets() const noexcept { return *m_targets; }

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
