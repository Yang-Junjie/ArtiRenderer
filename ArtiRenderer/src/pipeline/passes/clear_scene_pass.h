#pragma once
#include "linear_pass.h"

namespace arti::rendering {

// 清 SceneColor / SceneDepth。
//
// 独立成一个 pass 而不是让第一个绘制 pass 顺手清，是因为不透明材质已经拆成多个 pass：
// 让其中某一个负责清屏，就等于把「它必须排第一」这个约束藏进实现里，改顺序会静默出错。
// 放在 LinearStage::Clear 之后，这个约束由 stage 校验表达。
class ClearScenePass final : public LinearPass {
public:
    std::string_view name() const noexcept override { return "ClearScene"; }

    // 不需要 PSO，clear 是命令而不是绘制。
    void prepare(PassPrepareContext&) override {}
    void record(PassRecordContext& context) override;
};

} // namespace arti::rendering
