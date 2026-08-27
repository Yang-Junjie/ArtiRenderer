#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把 RenderTargetSet 的 SceneColor 用全屏三角形贴到 backbuffer。
// 离屏源是线性 RGBA8，backbuffer view 是 sRGB，编码由硬件在写入时完成。
//
// 不再持有上游 pass 的引用：SceneColor 从 targets() 拿，顺序由 LinearStage 表达
// （Opaque < Output，装错会在 addPass 时抛）。
class PresentPass final : public LinearPass {
public:
    PresentPass();
    ~PresentPass() override;

    std::string_view name() const noexcept override { return "Present"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
