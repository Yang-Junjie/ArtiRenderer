#include "render_target_set.h"

#include "log.h"

#include <algorithm>
#include <stdexcept>

namespace arti::rendering {
namespace {

// 场景是线性 HDR：PBR / IBL 的辐照度会远超 1.0，8 位归一化格式会在写入时就削顶，
// tone mapping 拿到的已经是削平的数据、救不回来。16F 也顺手解决了 8 位线性在暗部的带状。
constexpr auto sceneColorFormat = nvrhi::Format::RGBA16_FLOAT;
constexpr auto sceneDepthFormat = nvrhi::Format::D32;
// 显示层是 tone mapping 之后的 display-linear，值已经在 [0,1]，8 位够用。
// 不用 _SRGB 格式：编码留给 backbuffer 的 sRGB view 做，这一层保持和以前 SceneColor 相同的契约。
constexpr auto displayColorFormat = nvrhi::Format::RGBA8_UNORM;

// 目标只带 render target 用途，不开 isUAV。跟 compute 无关的目标不该付 storage usage 的代价
// （某些硬件会因此放弃 framebuffer 压缩）。将来真要 compute 原地改 SceneColor 时再加，那是
// 建纹理时的标志、事后加不了，所以届时要改的是这里。
nvrhi::TextureDesc makeColorDesc(uint32_t width, uint32_t height, nvrhi::Format format,
        const char* debug_name) {
    nvrhi::TextureDesc desc;
    desc.setWidth(width)
            .setHeight(height)
            .setFormat(format)
            .setIsRenderTarget(true)
            .setDebugName(debug_name)
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

    m_scene_color = device.createTexture(
            makeColorDesc(width, height, sceneColorFormat, "ArtiRenderer SceneColor"));
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

    // 只有 SceneColor 的 framebuffer，给 DeferredLightingPass 用：它把 SceneDepth 当 SRV 采，
    // 所以不能把同一张深度图挂成附件。
    nvrhi::FramebufferDesc scene_color_desc;
    scene_color_desc.addColorAttachment(m_scene_color);
    m_scene_color_framebuffer = device.createFramebuffer(scene_color_desc);
    if (!m_scene_color_framebuffer) {
        throw std::runtime_error("NVRHI failed to create the scene color framebuffer.");
    }

    // 显示层不带深度：TonemapPass 是一个覆盖全屏的三角形，没有可见性可言。
    m_display_color = device.createTexture(
            makeColorDesc(width, height, displayColorFormat, "ArtiRenderer DisplayColor"));
    if (!m_display_color) {
        throw std::runtime_error("NVRHI failed to create the display render target.");
    }
    nvrhi::FramebufferDesc display_desc;
    display_desc.addColorAttachment(m_display_color);
    m_display_framebuffer = device.createFramebuffer(display_desc);
    if (!m_display_framebuffer) {
        throw std::runtime_error("NVRHI failed to create the display framebuffer.");
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

nvrhi::IFramebuffer& RenderTargetSet::sceneColorFramebuffer() const {
    if (!m_scene_color_framebuffer) {
        throw std::logic_error("RenderTargetSet::sceneColorFramebuffer() before prepare().");
    }
    return *m_scene_color_framebuffer;
}

nvrhi::ITexture& RenderTargetSet::displayColor() const {
    if (!m_display_color) {
        throw std::logic_error("RenderTargetSet::displayColor() before prepare().");
    }
    return *m_display_color;
}

nvrhi::IFramebuffer& RenderTargetSet::displayFramebuffer() const {
    if (!m_display_framebuffer) {
        throw std::logic_error("RenderTargetSet::displayFramebuffer() before prepare().");
    }
    return *m_display_framebuffer;
}

nvrhi::IFramebuffer& RenderTargetSet::outputFramebuffer() const {
    if (m_output_framebuffer == nullptr) {
        throw std::logic_error("RenderTargetSet::outputFramebuffer() before prepare().");
    }
    return *m_output_framebuffer;
}

} // namespace arti::rendering
