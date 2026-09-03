#include "frame_context.h"

namespace arti::rendering {

FrameContext::FrameContext(const RenderScene& scene, const FrameOverlay& overlay,
        const detail::ResourceRegistry& resources, RenderOutputInfo output,
        const FrameSettings& settings) noexcept
    : m_scene(&scene)
    , m_overlay(&overlay)
    , m_resources(&resources)
    , m_output(output)
    , m_settings(settings)
{
    // 相机可见性在这里算完，pass 只读结果 —— 理由见头文件里 isVisible 的注释。
    m_camera_frustum = Frustum::fromViewProjection(scene.view.projection * scene.view.view);

    m_camera_visible.resize(scene.draws.size(), 1);
    for (std::size_t index = 0; index < scene.draws.size(); ++index) {
        const AABB& bounds = scene.draws[index].world_bounds;
        // 没有包围盒的 draw 不剔。Frustum::intersects 把空盒判为不可见，那是「盒子确实是空的」
        // 的语义；这里遇到的空盒是「不知道它在哪」，两回事。按不可见处理的话，一旦哪天
        // 上游漏填 world_bounds，症状是物体凭空消失；按可见处理最坏只是白画一个空网格。
        if (bounds.isEmpty()) {
            continue;
        }
        m_camera_visible[index] = m_camera_frustum.intersects(bounds) ? 1 : 0;
    }
}

} // namespace arti::rendering
