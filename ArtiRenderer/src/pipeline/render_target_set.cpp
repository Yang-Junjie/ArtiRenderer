#include "render_target_set.h"

#include "log.h"

#include <algorithm>
#include <stdexcept>

namespace arti::rendering {
namespace {

constexpr auto sceneColorFormat = nvrhi::Format::RGBA8_UNORM;
constexpr auto sceneDepthFormat = nvrhi::Format::D32;

// 目标只带 render target 用途，不开 isUAV。跟 compute 无关的目标不该付 storage usage 的代价
// （某些硬件会因此放弃 framebuffer 压缩）。将来真要 compute 原地改 SceneColor 时再加，那是
// 建纹理时的标志、事后加不了，所以届时要改的是这里。
nvrhi::TextureDesc makeColorDesc(uint32_t width, uint32_t height) {
    nvrhi::TextureDesc desc;
    desc.setWidth(width)
            .setHeight(height)
            .setFormat(sceneColorFormat)
            .setIsRenderTarget(true)
            .setDebugName("ArtiRenderer SceneColor")
            .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return desc;
}

nvrhi::TextureDesc makeDepthDesc(uint32_t width, uint32_t height) {
    nvrhi::TextureDesc desc;
    desc.setWidth(width)
            .setHeight(height)
            .setFormat(sceneDepthFormat)
            .setIsRenderTarget(true)
            .setDebugName("ArtiRenderer SceneDepth")
            .enableAutomaticStateTracking(nvrhi::ResourceStates::DepthWrite);
    return desc;
}

} // namespace

void RenderTargetSet::prepare(nvrhi::IDevice& device, nvrhi::IFramebuffer& output_framebuffer,
        uint32_t requested_width, uint32_t requested_height) {
    m_output_framebuffer = &output_framebuffer;

    const auto& output_info = output_framebuffer.getFramebufferInfo();
    if (output_info.width == 0 || output_info.height == 0) {
        throw std::invalid_argument("Render targets need a non-zero size.");
    }

    // 请求尺寸只要有一维是 0 就整个回退到输出尺寸 —— 半个请求没有意义。
    const bool use_requested = requested_width != 0 && requested_height != 0;
    // 面板被折叠或拖到极小时 ImGui 会给出 0 甚至负值（调用方转成 uint32 后可能极大），
    // 这里夹一下：为这种瞬时状态抛异常会把整个应用带走。
    const auto width = use_requested ? std::max(requested_width, 1u) : output_info.width;
    const auto height = use_requested ? std::max(requested_height, 1u) : output_info.height;
    if (m_scene_framebuffer && m_width == width && m_height == height) {
        return;
    }

    m_scene_color = device.createTexture(makeColorDesc(width, height));
    m_scene_depth = device.createTexture(makeDepthDesc(width, height));
    if (!m_scene_color || !m_scene_depth) {
        throw std::runtime_error("NVRHI failed to create the scene render targets.");
    }

    nvrhi::FramebufferDesc framebuffer_desc;
    framebuffer_desc.addColorAttachment(m_scene_color).setDepthAttachment(m_scene_depth);
    m_scene_framebuffer = device.createFramebuffer(framebuffer_desc);
    if (!m_scene_framebuffer) {
        throw std::runtime_error("NVRHI failed to create the scene framebuffer.");
    }

    m_width = width;
    m_height = height;
    ++m_revision;
    getLogChannel().debug("Scene targets resized to {}x{} (revision {})", width, height, m_revision);
}

nvrhi::ITexture& RenderTargetSet::sceneColor() const {
    if (!m_scene_color) {
        throw std::logic_error("RenderTargetSet::sceneColor() before prepare().");
    }
    return *m_scene_color;
}

nvrhi::ITexture& RenderTargetSet::sceneDepth() const {
    if (!m_scene_depth) {
        throw std::logic_error("RenderTargetSet::sceneDepth() before prepare().");
    }
    return *m_scene_depth;
}

nvrhi::IFramebuffer& RenderTargetSet::sceneFramebuffer() const {
    if (!m_scene_framebuffer) {
        throw std::logic_error("RenderTargetSet::sceneFramebuffer() before prepare().");
    }
    return *m_scene_framebuffer;
}

nvrhi::IFramebuffer& RenderTargetSet::outputFramebuffer() const {
    if (m_output_framebuffer == nullptr) {
        throw std::logic_error("RenderTargetSet::outputFramebuffer() before prepare().");
    }
    return *m_output_framebuffer;
}

} // namespace arti::rendering
