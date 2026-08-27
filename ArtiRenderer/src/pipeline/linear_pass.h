#pragma once
#include "artichoco/renderer/render_pass.h"
#include "frame_context.h"

#include <string_view>

namespace arti::rendering {


class PassPrepareContext {
public:
    PassPrepareContext(arti::renderer::RenderPassPrepareContext& rhi, FrameContext& frame) noexcept;

    nvrhi::IDevice& device() const noexcept;
    nvrhi::IFramebuffer& framebffer() const noexcept;
    const nvrhi::FramebufferInfoEx& framebfferInfo() const noexcept;
    size_t frameSlotCount() const noexcept;

    FrameContext& frame() const noexcept;
    PassBlackboard& blackboard() const noexcept;

private:
    arti::renderer::RenderPassPrepareContext* m_rhi{ nullptr };
    FrameContext* m_frame{ nullptr };
};


class PassRecordContext {
public:
    PassRecordContext(arti::renderer::RenderPassContext& rhi, FrameContext& frame) noexcept;

    nvrhi::IDevice& device() const noexcept;
    nvrhi::ICommandList& commands() const noexcept;
    nvrhi::IFramebuffer& framebffer() const noexcept;
    nvrhi::ITexture& framebfferColor() const noexcept;
    const nvrhi::FramebufferInfoEx& framebfferInfo() const noexcept;
    size_t frameSlotIndex() const noexcept;

    FrameContext& frame() const noexcept;
    const PassBlackboard& blackboard() const noexcept;

    nvrhi::IBuffer& vertexBuffer(MeshHandle mesh) const;
    nvrhi::IBuffer& indexBuffer(MeshHandle mesh) const;
    nvrhi::ITexture& texture(TextureHandle handle) const;

private:
    arti::renderer::RenderPassContext* m_rhi{ nullptr };
    FrameContext* m_frame{ nullptr };
};


class LinearPass {
public:
    virtual ~LinearPass() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual bool isEnabled(const FrameContext&) const { return true; }

    virtual void prepare(PassPrepareContext& context) = 0;

    virtual void record(PassRecordContext& context) = 0;
};

} // namespace arti::rendering
