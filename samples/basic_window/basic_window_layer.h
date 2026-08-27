#pragma once
#include "artichoco/core/layer.h"
#include "arti_renderer.h"

#include <cstdint>

#include <glm/vec3.hpp>
#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::sample {

class ImGuiHost;

// 通过 rendering::Renderer 画一个旋转的立方体，外加一个 ImGui 面板，
// 用来验证 Renderer -> LinearPipeline -> pass -> RHI 整条链路。
class BasicWindowLayer final : public core::Layer {
public:
    BasicWindowLayer(bool enable_renderer, uint32_t frame_limit, bool show_imgui_demo = false,
            bool editor_mode = false);
    ~BasicWindowLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep delta_time) override;
    void onImGuiRender() override;
    void onRender() override;

private:
    void createSceneResources();
    void drawUI();
    void drawViewportPanel();

    bool m_enable_renderer{ false };
    uint32_t m_frame_limit{ 0 };
    uint32_t m_frame_index{ 0 };
    float m_elapsed_seconds{ 0.0f };

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<ImGuiHost> m_imgui;

    // UI 里能调的东西，onRender 每帧读。
    bool m_show_demo_window{ false };
    bool m_rotate{ true };
    bool m_editor_mode{ false };
    glm::vec3 m_clear_color{ 0.05f, 0.07f, 0.10f };
    rendering::FrameStatistics m_last_statistics;

    // 编辑器模式下场景该渲染成多大。来自 Viewport 面板的内容区尺寸，也就是 ImGui 上一帧的布局，
    // 所以拖动面板边缘时场景会晚一帧跟上 —— 编辑器都这样。
    uint32_t m_scene_width{ 0 };
    uint32_t m_scene_height{ 0 };

    rendering::MeshHandle m_cube_mesh;
    rendering::MaterialHandle m_cube_material;
    rendering::TextureHandle m_checker_texture;
};

} // namespace arti::sample
