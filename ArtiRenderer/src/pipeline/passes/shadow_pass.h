#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 方向光的级联阴影深度图。把场景几何按每一级的光源视锥重画一遍，只写深度。
//
// 目前是**空壳**：isEnabled() 恒 false，深度图建了但没人往里画，也没人采。装上它是为了先把
// 管线接缝（ShadowTargets、LinearStage::Shadow、两个 context 多出来的第四个资源）走通，
// 这一步画面不该有任何变化。见 docs/tasks/2026-09-02-directional-csm.md 的阶段 1。
//
// 只有一个方向光能投阴影（多方向光各带一套 cascade 会让显存和 pass 数翻倍）。选中的是第一个
// casts_shadow 为真的方向光，其余方向光照常照明但不投影。
class ShadowPass final : public LinearPass {
public:
    ShadowPass();
    ~ShadowPass() override;

    std::string_view name() const noexcept override { return "Shadow"; }

    bool isEnabled(const FrameContext& frame) const override;

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
