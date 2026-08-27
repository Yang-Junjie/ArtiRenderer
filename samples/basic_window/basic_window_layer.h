#pragma once
#include "artichoco/core/layer.h"

#include <cstdint>

#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::sample {

// 最小可运行的层：创建 RenderDevice 并每帧清屏，用于验证
// ArtiRenderer -> ArtiChoco -> Vulkan/NVRHI 这条链路是通的。
class BasicWindowLayer final : public core::Layer {
public:
    BasicWindowLayer(bool enable_renderer, uint32_t frame_limit);
    ~BasicWindowLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep delta_time) override;
    void onRender() override;

private:
    bool m_enable_renderer{ false };
    uint32_t m_frame_limit{ 0 };
    uint32_t m_frame_index{ 0 };
    float m_elapsed_seconds{ 0.0f };
    std::unique_ptr<renderer::RenderDevice> m_render_device;
};

} // namespace arti::sample
