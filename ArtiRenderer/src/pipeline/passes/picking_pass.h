#pragma once
#include "linear_pass.h"

#include <memory>
#include <optional>

namespace arti::rendering {

// GPU 拾取。把每个 draw 的 picking_id 画进一张 R32_UINT 缓冲，再把请求的那个像素拷进
// staging 纹理读回来。
//
// 为什么不用 CPU 射线求交：那需要在 CPU 侧保留网格数据（现在上传完就丢了），而且对
// 蒙皮、位移贴图、alpha 裁剪这些「顶点位置不等于视觉位置」的情况会给出错的答案。
// GPU 拾取直接问「这个像素是谁画的」，定义上就和看到的一致。
//
// 读回是异步的：这一帧发起拷贝，几帧之后才 map 得到结果（按 frame slot 分开，
// 所以不会 stall GPU 等自己刚提交的活）。因此 picking_id 必须跨帧稳定 ——
// 逐帧重编号的话结果回来时编号表已经变了。这个约束由调用方保证，见 DrawItem::picking_id。
//
// 遮挡由这个 pass 自己的深度缓冲决定（每次拾取清成 1.0，Less 且写入），而不是复用 GBuffer
// 写好的那张做 LessOrEqual：后者要求两个 pass 的顶点变换逐位一致，而它们的 MVP 一个在 CPU
// 侧乘好、一个在着色器里分两步乘，差一个 ULP 片元就被丢掉 —— 表现成拾取时灵时不灵。
// 详见 picking_pass.cpp 里 Impl::depth_texture 上面那段。
class PickingPass final : public LinearPass {
public:
    PickingPass();
    ~PickingPass() override;

    std::string_view name() const noexcept override { return "Picking"; }

    // 没有拾取请求的帧整个跳过 —— shader 不编译，ID 缓冲不建。
    bool isEnabled(const FrameContext& frame) const override;

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

    // 取走已经读回来的结果（取走即清空）。还没准备好时返回空。
    std::optional<PickResult> takeResult() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
