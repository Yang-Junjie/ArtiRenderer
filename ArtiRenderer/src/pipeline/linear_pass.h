#pragma once
#include "artichoco/renderer/render_pass.h"
#include "frame_context.h"
#include "render_target_set.h"

#include <string_view>

namespace arti::rendering {


class PassPrepareContext {
public:
    PassPrepareContext(arti::renderer::RenderPassPrepareContext& rhi, FrameContext& frame,
            RenderTargetSet& targets) noexcept;

    nvrhi::IDevice& device() const noexcept;

    FrameContext& frame() const noexcept;
    // 目标已经由管线建好了，pass 直接用。想知道尺寸就问它要 framebuffer info。
    RenderTargetSet& targets() const noexcept;

private:
    arti::renderer::RenderPassPrepareContext* m_rhi{ nullptr };
    FrameContext* m_frame{ nullptr };
    RenderTargetSet* m_targets{ nullptr };
};


class PassRecordContext {
public:
    PassRecordContext(arti::renderer::RenderPassContext& rhi, FrameContext& frame,
            RenderTargetSet& targets) noexcept;

    nvrhi::IDevice& device() const noexcept;
    nvrhi::ICommandList& commands() const noexcept;

    FrameContext& frame() const noexcept;
    RenderTargetSet& targets() const noexcept;

    nvrhi::IBuffer& vertexBuffer(MeshHandle mesh) const;
    nvrhi::IBuffer& indexBuffer(MeshHandle mesh) const;
    nvrhi::ITexture& texture(TextureHandle handle) const;

private:
    arti::renderer::RenderPassContext* m_rhi{ nullptr };
    FrameContext* m_frame{ nullptr };
    RenderTargetSet* m_targets{ nullptr };
};


// compute pass 也是这个接口：compute PSO 不依赖 framebuffer（createComputePipeline 不吃
// framebuffer 参数），record() 里 setComputeState + dispatch 就行，不需要新的 pass 种类。
class LinearPass {
public:
    virtual ~LinearPass() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual bool isEnabled(const FrameContext&) const { return true; }

    virtual void prepare(PassPrepareContext& context) = 0;

    virtual void record(PassRecordContext& context) = 0;
};

} // namespace arti::rendering
