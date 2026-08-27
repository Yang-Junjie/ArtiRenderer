#pragma once
#include "render_output.h"
#include "render_scene.h"
#include "renderer.h"
#include "resource_registry.h"

namespace arti::rendering {

// 一帧里所有 pass 共享的东西。Renderer 每帧构造一次，pass 不持有它。
class FrameContext {
public:
    FrameContext(const RenderScene& scene, const detail::ResourceRegistry& resources,
            RenderOutputInfo output) noexcept;

    const RenderScene& scene() const noexcept { return *m_scene; }

    const RenderView& view() const noexcept { return m_scene->view; }

    const detail::ResourceRegistry& resources() const noexcept { return *m_resources; }

    RenderOutputInfo output() const noexcept { return m_output; }

    FrameStatistics& statistics() noexcept { return m_statistics; }
    const FrameStatistics& statistics() const noexcept { return m_statistics; }

private:
    const RenderScene* m_scene{ nullptr };
    const detail::ResourceRegistry* m_resources{ nullptr };
    RenderOutputInfo m_output;
    FrameStatistics m_statistics;
};

} // namespace arti::rendering
