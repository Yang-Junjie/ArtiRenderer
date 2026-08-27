#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把宿主交来的一帧 ImDrawData 变成 draw call，画进 backbuffer。装在 LinearStage::UI，所以跑在
// PresentPass 之后 —— 场景已经在 backbuffer 里了，这个 pass 只往上盖，不清屏。
//
// pass 不碰 ImGui context：谁 NewFrame/Render 是宿主的事，这里只认 FrameOverlay 里的指针。
// 没有 draw data 时 isEnabled() 返回 false，连 shader 都不会编译。
//
// 纹理身份直接用 TextureHandle::value() 当 ImTextureID（见 frame_overlay.h 的 imguiTextureId），
// 所以不需要一层 UI 专用的纹理注册表，renderer 里任何一张纹理都能喂给 ImGui::Image()。
class ImGuiPass final : public LinearPass {
public:
    ImGuiPass();
    ~ImGuiPass() override;

    std::string_view name() const noexcept override { return "ImGui"; }

    bool isEnabled(const FrameContext& frame) const override;

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
