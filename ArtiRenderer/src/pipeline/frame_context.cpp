#include "frame_context.h"

namespace arti::rendering {

FrameContext::FrameContext(const RenderScene& scene, const detail::ResourceRegistry& resources,
        RenderOutputInfo output) noexcept
    : m_scene(&scene)
    , m_resources(&resources)
    , m_output(output)
{}

} // namespace arti::rendering
