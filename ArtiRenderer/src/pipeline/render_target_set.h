#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace arti::rendering {

// 一帧里所有 pass 共用的渲染目标。由 LinearPipeline 拥有，pass 通过上下文的 targets() 借用。
//
// 目标搬到这里而不是留在产出它的 pass 里，是为了解开 pass 之间的耦合：下游想读 SceneColor
// 不必认识上游 pass 的具体类型，中间插一个 pass 也不用改谁的构造函数。
//
// 名字是固定的、不是泛型的 slot 机制：这几个名字承载了色彩空间契约（scene 是场景线性，
// output 是编码后的 backbuffer），泛型槽谁都能造一个新目标，这层含义就没了。也不按名字查表 ——
// 那是已经删掉的 PassBlackboard 的形状。
//
// 现在只有 scene 和 output 两层。tone mapping 之后的 display-linear 层还不存在（SceneColor 是
// RGBA8_UNORM 线性，backbuffer view 是 sRGB，编码由硬件在写入时完成），等真做 tone mapping 再加。
class RenderTargetSet {
public:
    RenderTargetSet() = default;

    RenderTargetSet(const RenderTargetSet&) = delete;
    RenderTargetSet& operator=(const RenderTargetSet&) = delete;

    // 每帧在所有 pass 的 prepare() 之前调用：绑定本帧 backbuffer，并按它的尺寸建离屏目标
    // （尺寸没变就复用）。
    //
    // backbuffer 在 prepare 阶段就绑好而不是留到 record，是因为 pass 的 prepare() 要用
    // outputFramebuffer() 的 framebuffer info 去建 PSO —— 留到 record 绑的话第一帧就会抛。
    void prepare(nvrhi::IDevice& device, nvrhi::IFramebuffer& output_framebuffer);

    bool isReady() const noexcept { return bool(m_scene_framebuffer); }

    // 重建时自增。下游 pass 靠它判断要不要重建 binding set —— 比自己存纹理指针再比对更明确。
    uint64_t revision() const noexcept { return m_revision; }

    nvrhi::ITexture& sceneColor() const;
    nvrhi::ITexture& sceneDepth() const;
    nvrhi::IFramebuffer& sceneFramebuffer() const;

    nvrhi::IFramebuffer& outputFramebuffer() const;

private:
    nvrhi::TextureHandle m_scene_color;
    nvrhi::TextureHandle m_scene_depth;
    nvrhi::FramebufferHandle m_scene_framebuffer;
    nvrhi::IFramebuffer* m_output_framebuffer{ nullptr };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint64_t m_revision{ 0 };
};

} // namespace arti::rendering
