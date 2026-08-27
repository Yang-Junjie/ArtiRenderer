#pragma once
#include "artichoco/core/layer.h"
#include "arti_renderer.h"

#include <cstdint>

#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::sample {

// 通过 rendering::Renderer 画一个旋转的立方体，
// 用来验证 Renderer -> LinearPipeline -> pass -> RHI 整条链路。
class BasicWindowLayer final : public core::Layer {
public:
    BasicWindowLayer(bool enable_renderer, uint32_t frame_limit);
    ~BasicWindowLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep delta_time) override;
    void onRender() override;

private:
    void createSceneResources();

    bool m_enable_renderer{ false };
    uint32_t m_frame_limit{ 0 };
    uint32_t m_frame_index{ 0 };
    float m_elapsed_seconds{ 0.0f };

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;

    rendering::MeshHandle m_cube_mesh;
    rendering::MaterialHandle m_cube_material;
    rendering::TextureHandle m_checker_texture;
};

} // namespace arti::sample
