#include "basic_window_layer.h"

#include "artichoco/core/application.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/renderer/render_device.h"

#include <array>
#include <cmath>
#include <utility>

namespace arti::sample {

BasicWindowLayer::BasicWindowLayer(bool enable_renderer, uint32_t frame_limit)
    : Layer("BasicWindowLayer")
    , m_enable_renderer(enable_renderer)
    , m_frame_limit(frame_limit)
{}

BasicWindowLayer::~BasicWindowLayer() = default;

void BasicWindowLayer::onAttach()
{
    auto& app = core::Application::get();

    if (!m_enable_renderer) {
        app.getLogChannel().info("Renderer disabled, running window loop only");
        return;
    }

    auto surface_source = platform::createSDLVulkanSurfaceSource(app.getWindow());
    renderer::RenderDeviceCreateInfo device_info;
    device_info.application_name = "ArtiRenderer Basic Window";
    m_render_device = std::make_unique<renderer::RenderDevice>(
            app.getWindow(), std::move(surface_source), device_info);

    const auto swapchain = m_render_device->swapchainInfo();
    app.getLogChannel().info("Render device ready, swapchain {}x{} (available: {})",
            swapchain.width, swapchain.height, swapchain.available);
}

void BasicWindowLayer::onDetach()
{
    if (m_render_device) {
        m_render_device->waitIdle();
        m_render_device.reset();
    }
}

void BasicWindowLayer::onUpdate(core::Timestep delta_time)
{
    m_elapsed_seconds += delta_time.getSeconds();
    ++m_frame_index;

    if (m_frame_limit != 0 && m_frame_index >= m_frame_limit) {
        core::Application::get().getLogChannel().info(
                "Frame limit reached after {} frames", m_frame_index);
        core::Application::get().close();
    }
}

void BasicWindowLayer::onRender()
{
    if (!m_render_device) {
        return;
    }

    const float pulse = 0.5f + (0.5f * std::sin(m_elapsed_seconds));
    const std::array<float, 4> clear_color{ 0.1f, 0.1f + (0.4f * pulse), 0.2f, 1.0f };
    m_render_device->renderNvrhiClearFrame(clear_color);
}

} // namespace arti::sample
