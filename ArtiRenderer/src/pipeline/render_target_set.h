#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace arti::rendering {

// 一帧里所有 pass 共用的渲染目标。由 LinearPipeline 拥有，pass 通过上下文的 targets() 借用。
//
// 目标搬到这里而不是留在产出它的 pass 里，是为了解开 pass 之间的耦合：下游想读 SceneColor
// 不必认识上游 pass 的具体类型，中间插一个 pass 也不用改谁的构造函数。
//
// 名字是固定的、不是泛型的 slot 机制：这几个名字承载了色彩空间契约，泛型槽谁都能造一个新目标，
// 这层含义就没了。也不按名字查表 —— 那是已经删掉的 PassBlackboard 的形状。
//
// 三层，色彩空间逐层收窄：
//   scene   —— 场景线性、HDR（RGBA16_FLOAT）。光照结果直接写这里，值可以远超 1.0。
//   display —— 显示线性、LDR（RGBA8_UNORM）。TonemapPass 把 scene 压进 [0,1] 的产物。
//   output  —— 本帧 backbuffer，view 是 sRGB，编码由硬件在写入时完成。
//
// scene 和 display 分开是 tone mapping 的必要配套：编辑器模式下 PresentPass 不跑，场景是被
// ImGui 当纹理采的，所以压缩必须发生在一个**纹理**里，不能只在写 backbuffer 时顺手做。
class RenderTargetSet {
public:
    RenderTargetSet() = default;

    RenderTargetSet(const RenderTargetSet&) = delete;
    RenderTargetSet& operator=(const RenderTargetSet&) = delete;

    // 每帧在所有 pass 的 prepare() 之前调用：绑定本帧 backbuffer，并建离屏目标（尺寸没变就复用）。
    //
    // backbuffer 在 prepare 阶段就绑好而不是留到 record，是因为 pass 的 prepare() 要用
    // outputFramebuffer() 的 framebuffer info 去建 PSO —— 留到 record 绑的话第一帧就会抛。
    //
    // requested_width/height 为 0 时离屏目标跟着 backbuffer 的尺寸走。编辑器模式下宿主会传
    // 面板的像素尺寸，这样场景就是按面板分辨率渲染的，而不是渲染成窗口大小再缩放。
    void prepare(nvrhi::IDevice& device, nvrhi::IFramebuffer& output_framebuffer,
            uint32_t requested_width = 0, uint32_t requested_height = 0);

    bool isReady() const noexcept { return bool(m_scene_framebuffer); }

    // 重建时自增。下游 pass 靠它判断要不要重建 binding set —— 比自己存纹理指针再比对更明确。
    uint64_t revision() const noexcept { return m_revision; }

    nvrhi::ITexture& sceneColor() const;
    nvrhi::ITexture& sceneDepth() const;
    nvrhi::IFramebuffer& sceneFramebuffer() const;

    // 只挂 SceneColor、不挂深度。DeferredLightingPass 要的是这个：它把 SceneDepth 当 SRV 采
    // （从深度反投影出世界坐标），同一张图不能同时是深度附件 —— Vulkan 的 feedback loop。
    nvrhi::IFramebuffer& sceneColorFramebuffer() const;

    nvrhi::ITexture& displayColor() const;
    nvrhi::IFramebuffer& displayFramebuffer() const;

    // DisplayColor + SceneDepth。DebugLinePass 要的是这个：它画在显示层上，但要拿场景的深度
    // 做遮挡测试。displayFramebuffer() 不挂深度 —— TonemapPass 是全屏三角形，没有可见性可言。
    nvrhi::IFramebuffer& displayDepthFramebuffer() const;

    nvrhi::IFramebuffer& outputFramebuffer() const;

private:
    nvrhi::TextureHandle m_scene_color;
    nvrhi::TextureHandle m_scene_depth;
    nvrhi::FramebufferHandle m_scene_framebuffer;
    nvrhi::FramebufferHandle m_scene_color_framebuffer;
    nvrhi::TextureHandle m_display_color;
    nvrhi::FramebufferHandle m_display_framebuffer;
    nvrhi::FramebufferHandle m_display_depth_framebuffer;
    nvrhi::IFramebuffer* m_output_framebuffer{ nullptr };
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    uint64_t m_revision{ 0 };
};

} // namespace arti::rendering
