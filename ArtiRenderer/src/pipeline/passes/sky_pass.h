#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把环境 cube 画成天空背景。全屏三角形 + 逆 view-projection 反投影出视线方向。
//
// 排在 Opaque 之后（LinearStage::Sky）：深度测试 LessOrEqual、不写深度，所以只填没被物体
// 覆盖的像素。反过来先画天空的话，每个被挡住的像素都要白画一遍。
//
// 环境没开、没勾 sky_visible、或者烘焙还没就绪时整个 pass 跳过 —— 此时背景是 ClearScenePass
// 留下的 clear_color。
class SkyPass final : public LinearPass {
public:
    SkyPass();
    ~SkyPass() override;

    std::string_view name() const noexcept override { return "Sky"; }

    bool isEnabled(const FrameContext& frame) const override;

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
