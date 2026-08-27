#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 从黑板取 SceneColor，全屏三角形贴到 backbuffer。
//
// 离屏目标是线性 RGBA8，backbuffer view 是 sRGB，所以写入时由硬件做 sRGB 编码。
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
