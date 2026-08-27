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
        FrameContext& frame) noexcept
        : m_rhi(&rhi),
          m_frame(&frame) {}

nvrhi::IDevice& PassPrepareContext::device() const noexcept { return m_rhi->device(); }

nvrhi::IFramebuffer& PassPrepareContext::framebuffer() const noexcept {
    return m_rhi->framebuffer();
}

const nvrhi::FramebufferInfoEx& PassPrepareContext::framebufferInfo() const noexcept {
    return m_rhi->framebufferInfo();
}

size_t PassPrepareContext::frameSlotCount() const noexcept { return m_rhi->frameSlotCount(); }

FrameContext& PassPrepareContext::frame() const noexcept { return *m_frame; }

PassBlackboard& PassPrepareContext::blackboard() const noexcept { return m_frame->blackboard(); }

PassRecordContext::PassRecordContext(arti::renderer::RenderPassContext& rhi,
        FrameContext& frame) noexcept
        : m_rhi(&rhi),
          m_frame(&frame) {}

nvrhi::IDevice& PassRecordContext::device() const noexcept { return m_rhi->device(); }

nvrhi::ICommandList& PassRecordContext::commands() const noexcept { return m_rhi->commands(); }

nvrhi::IFramebuffer& PassRecordContext::framebuffer() const noexcept { return m_rhi->framebuffer(); }

nvrhi::ITexture& PassRecordContext::framebufferColor() const noexcept {
    return m_rhi->colorTexture();
}

const nvrhi::FramebufferInfoEx& PassRecordContext::framebufferInfo() const noexcept {
    return m_rhi->framebufferInfo();
}

size_t PassRecordContext::frameSlotIndex() const noexcept { return m_rhi->frameSlotIndex(); }

FrameContext& PassRecordContext::frame() const noexcept { return *m_frame; }

const PassBlackboard& PassRecordContext::blackboard() const noexcept {
    return m_frame->blackboard();
}

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
