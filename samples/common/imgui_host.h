#pragma once
#include "arti_renderer.h"

#include <cstdint>

struct ImDrawData;
struct ImGuiContext;

namespace arti::core {
class Window;
} // namespace arti::core

namespace arti::platform {
class SDLWindow;
} // namespace arti::platform

namespace arti::sample {

struct ImGuiHostCreateInfo {
    bool docking{ true };
    // false 时不落 imgui.ini。测试要的是每次都从同一个布局启动，而不是继承上一次跑的结果。
    bool persist_layout{ true };
};

// 宿主侧的 ImGui：context、SDL3 平台后端、字体图集。renderer 不链 Platform/SDL，所以这部分
// 只能在宿主这边 —— renderer 那边只认 FrameOverlay 里的一个 ImDrawData 指针。
//
// 字体图集走 Renderer::createTexture 变成一张普通纹理，再把 imguiTextureId() 交给 ImGui。
// 也就是说 UI 纹理和场景纹理是同一套资源，没有第二条上传路径。
class ImGuiHost {
public:
    // 需要 SDL 窗口（内部 dynamic_cast，不是就抛）。
    // renderer 用来建字体图集纹理，必须比本对象活得久。
    ImGuiHost(core::Window& window, rendering::Renderer& renderer,
            const ImGuiHostCreateInfo& create_info = {});
    ~ImGuiHost();

    ImGuiHost(const ImGuiHost&) = delete;
    ImGuiHost& operator=(const ImGuiHost&) = delete;

    // 成对调用，中间画 UI。endFrame() 之后 drawData() 才有效。
    void beginFrame();
    void endFrame();

    // 铺满主视口的停靠区，在 beginFrame() 之后、画其它窗口之前调用一次。
    //
    // 中央节点是透传的（PassthruCentralNode）：空着的时候场景直接透上来，所以 Direct 模式下
    // 不用把场景搬进纹理就已经是编辑器的样子了。想让场景进面板再开 PresentMode::IntoUI。
    //
    // 没开 docking 时是空操作，宿主不用自己判。
    void dockSpaceOverViewport();

    [[nodiscard]] bool isDockingEnabled() const noexcept { return m_docking; }

    // 交给 Renderer::renderFrame 的 overlay。endFrame() 之前是空的。
    [[nodiscard]] rendering::FrameOverlay overlay() const noexcept;

    // 鼠标/键盘是否被 UI 吃掉了。宿主用它决定要不要把输入喂给相机之类的东西。
    [[nodiscard]] bool wantsMouseInput() const noexcept;
    [[nodiscard]] bool wantsKeyboardInput() const noexcept;

private:
    void createFontTexture();

    platform::SDLWindow& m_window;
    rendering::Renderer& m_renderer;
    ImGuiContext* m_context{ nullptr };
    ImDrawData* m_draw_data{ nullptr };
    uint64_t m_event_observer_id{ 0 };
    bool m_frame_started{ false };
    bool m_docking{ false };
    rendering::TextureHandle m_font_texture;
};

} // namespace arti::sample
