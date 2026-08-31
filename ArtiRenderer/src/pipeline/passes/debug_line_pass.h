#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 调试线：把 FrameSettings::debug_lines 里的世界空间线段画到 DisplayColor 上。
//
// 画在**显示层**而不是 SceneColor：这个 pass 排在 TonemapPass 之后，所以调用方给的颜色
// 就是屏幕上看到的颜色。反过来画在场景层的话，曝光和 tone 曲线会把「用红绿蓝区分含义」
// 这件事毁掉 —— 而那正是调试绘制存在的理由。
//
// 深度测试开、深度写关：线在世界里有位置，被物体挡住就该看不见；但线之间不互相遮挡。
// 深度借的是 RenderTargetSet 的 SceneDepth，所以目标是 displayDepthFramebuffer()
// —— 常规的 displayFramebuffer() 不挂深度。
//
// 顶点缓冲按 frame slot 开数组（照 ImGuiPass 的形状）：多帧在飞的时候，下一帧的 writeBuffer
// 会撞上上一帧还在读的那份。自动状态跟踪会插 barrier 保正确，但那就变成一个跨帧串行点。
//
// 没有线的帧整个跳过 —— shader 不编译、缓冲不建，和 PickingPass 一个套路。
class DebugLinePass final : public LinearPass {
public:
    DebugLinePass();
    ~DebugLinePass() override;

    std::string_view name() const noexcept override { return "DebugLine"; }

    bool isEnabled(const FrameContext& frame) const override;

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
