#pragma once

#include <cstdint>

namespace arti::rendering {

// Where does the final frame end up? Currently, the only destination is the swapchain.
// In the future, we may support other destinations like a offscreen texture.
enum class RenderTargetKind : uint8_t {
    Swapchain,
};

struct RenderOutputInfo {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    RenderTargetKind kind{ RenderTargetKind::Swapchain };
    // when window minimized, swapchain is not renderable, this frame should be skipped.
    bool available{ false };
};

} // namespace arti::rendering
