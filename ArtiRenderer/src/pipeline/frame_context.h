#pragma once
#include "frame_overlay.h"
#include "render_output.h"
#include "render_scene.h"
#include "renderer.h"
#include "resource_registry.h"

namespace arti::rendering {

// Renderer 上那几个跨帧的开关，每帧快照进 FrameContext。单独成一个 struct 是为了让
// FrameContext 的构造参数不再往上长 —— 以后加开关只动这里。
struct FrameSettings {
    PresentMode present{ PresentMode::Direct };
    // 0 表示跟着输出尺寸走。
    uint32_t scene_target_width{ 0 };
    uint32_t scene_target_height{ 0 };
    // ImGuiPass 靠它认出「这次 draw 要的是 SceneColor」，从而绑 RenderTargetSet 而不是查注册表。
    TextureHandle scene_color_id;
};

// 一帧里所有 pass 共享的东西。Renderer 每帧构造一次，pass 不持有它。
class FrameContext {
public:
    FrameContext(const RenderScene& scene, const FrameOverlay& overlay,
            const detail::ResourceRegistry& resources, RenderOutputInfo output,
            const FrameSettings& settings) noexcept;

    const FrameSettings& settings() const noexcept { return m_settings; }

    const RenderScene& scene() const noexcept { return *m_scene; }

    const FrameOverlay& overlay() const noexcept { return *m_overlay; }

    const RenderView& view() const noexcept { return m_scene->view; }

    const detail::ResourceRegistry& resources() const noexcept { return *m_resources; }

    RenderOutputInfo output() const noexcept { return m_output; }

    FrameStatistics& statistics() noexcept { return m_statistics; }
    const FrameStatistics& statistics() const noexcept { return m_statistics; }

private:
    const RenderScene* m_scene{ nullptr };
    const FrameOverlay* m_overlay{ nullptr };
    const detail::ResourceRegistry* m_resources{ nullptr };
    RenderOutputInfo m_output;
    FrameSettings m_settings;
    FrameStatistics m_statistics;
};

} // namespace arti::rendering
