#pragma once
#include "linear_pass.h"

namespace arti::rendering {

// 清 SceneDepth 和三张 G-Buffer。
//
// **不清 SceneColor**：延迟管线下 DeferredLightingPass 是 SceneColor 每一个像素的唯一写入者
// （全屏三角形、无深度测试、无 discard），没有几何体的地方由它输出 clear_color。背景色因此
// 只有一个来源，不需要两处「说好保持一致」。
//
// 独立成一个 pass 而不是让第一个绘制 pass 顺手清，是因为几何写入已经按 G-Buffer 编码拆成了
// 多个 pass：让其中某一个负责清屏，就等于把「它必须排第一」这个约束藏进实现里，改顺序会静默
// 出错。放在 LinearStage::Clear 之后，这个约束由 stage 校验表达。
class ClearScenePass final : public LinearPass {
public:
    std::string_view name() const noexcept override { return "ClearScene"; }

    // 不需要 PSO，clear 是命令而不是绘制。
    void prepare(PassPrepareContext&) override {}
    void record(PassRecordContext& context) override;
};

} // namespace arti::rendering
