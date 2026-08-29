#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把 RenderTargetSet 的 DisplayColor 用全屏三角形贴到 backbuffer。
// 离屏源是 tone mapping 之后的显示线性 RGBA8，backbuffer view 是 sRGB，编码由硬件在写入时完成。
//
// 不再持有上游 pass 的引用：DisplayColor 从 targets() 拿，顺序由 LinearStage 表达
// （PostProcess < Output，装错会在 addPass 时抛）。
class PresentPass final : public LinearPass {
public:
    PresentPass();
    ~PresentPass() override;

    std::string_view name() const noexcept override { return "Present"; }

    // IntoUI 模式下关掉：场景不贴到 backbuffer，而是留在 DisplayColor 里等 ImGui 采样。
    bool isEnabled(const FrameContext& frame) const override;

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
