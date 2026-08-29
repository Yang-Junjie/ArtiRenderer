#pragma once
#include "artichoco/renderer/render_pass.h"
#include "environment_resources.h"
#include "frame_context.h"
#include "render_target_set.h"

#include <cstddef>
#include <string_view>

namespace arti::rendering {


class PassPrepareContext {
public:
    PassPrepareContext(arti::renderer::RenderPassPrepareContext& rhi, FrameContext& frame,
            RenderTargetSet& targets, EnvironmentResources& environment) noexcept;

    nvrhi::IDevice& device() const noexcept;

    // 飞行中的帧数。每帧一份的资源（比如上传型顶点缓冲）按它开数组，避免覆盖 GPU 还在读的那份。
    size_t frameSlotCount() const noexcept;

    FrameContext& frame() const noexcept;
    // 目标已经由管线建好了，pass 直接用。想知道尺寸就问它要 framebuffer info。
    RenderTargetSet& targets() const noexcept;
    // IBL 的烘焙产物。EnvironmentBakePass 写，下游读。
    EnvironmentResources& environment() const noexcept;

private:
    arti::renderer::RenderPassPrepareContext* m_rhi{ nullptr };
    FrameContext* m_frame{ nullptr };
    RenderTargetSet* m_targets{ nullptr };
    EnvironmentResources* m_environment{ nullptr };
};


class PassRecordContext {
public:
    PassRecordContext(arti::renderer::RenderPassContext& rhi, FrameContext& frame,
            RenderTargetSet& targets, EnvironmentResources& environment) noexcept;

    nvrhi::IDevice& device() const noexcept;
    nvrhi::ICommandList& commands() const noexcept;

    // 本帧用哪个槽位。和 prepare 时的 frameSlotCount() 配套。
    size_t frameSlotIndex() const noexcept;

    // 本帧 backbuffer 的颜色纹理。清屏这类整张纹理的操作要的是纹理本身而不是 framebuffer。
    nvrhi::ITexture& outputColor() const noexcept;

    FrameContext& frame() const noexcept;
    RenderTargetSet& targets() const noexcept;
    EnvironmentResources& environment() const noexcept;

    nvrhi::IBuffer& vertexBuffer(MeshHandle mesh) const;
    nvrhi::IBuffer& indexBuffer(MeshHandle mesh) const;
    nvrhi::ITexture& texture(TextureHandle handle) const;

private:
    arti::renderer::RenderPassContext* m_rhi{ nullptr };
    FrameContext* m_frame{ nullptr };
    RenderTargetSet* m_targets{ nullptr };
    EnvironmentResources* m_environment{ nullptr };
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
