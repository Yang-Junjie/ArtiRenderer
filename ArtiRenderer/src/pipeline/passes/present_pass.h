#pragma once
#include "forward_opaque_pass.h"
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把 ForwardOpaquePass 的离屏结果用全屏三角形贴到 backbuffer。
// 离屏目标是线性 RGBA8，backbuffer view 是 sRGB，编码由硬件在写入时完成。
//
// 直接持有上游 pass 的引用，所以在管线里必须排在它后面：
// RenderDevice::renderFrame 先按顺序跑完所有 prepare()，再按顺序跑 record()，
// 上游的 prepare() 建好离屏纹理之后这里才读得到。
class PresentPass final : public LinearPass {
public:
    explicit PresentPass(const ForwardOpaquePass& source);
    ~PresentPass() override;

    std::string_view name() const noexcept override { return "Present"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
