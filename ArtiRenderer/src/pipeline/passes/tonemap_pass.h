#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 场景线性 HDR → 显示线性 LDR。读 RenderTargetSet 的 SceneColor，全屏三角形写 DisplayColor。
//
// 独立成 pass 而不是塞进 PresentPass，是因为 IntoUI 模式下 PresentPass 根本不跑（场景被 ImGui
// 当纹理采）。压缩必须发生在一张纹理里，两条呈现路径才能看到同一个画面。
//
// 常驻启用：没有它 DisplayColor 就是未初始化的，下游拿到的是垃圾。
class TonemapPass final : public LinearPass {
public:
    TonemapPass();
    ~TonemapPass() override;

    std::string_view name() const noexcept override { return "Tonemap"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
