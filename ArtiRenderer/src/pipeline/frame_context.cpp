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
{}

} // namespace arti::rendering
