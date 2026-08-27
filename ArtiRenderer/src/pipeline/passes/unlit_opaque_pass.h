#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 画 MaterialType::Unlit 的不透明物体：base_color * base_color_texture，不参与光照。
//
// 不清屏 —— 那是 ClearScenePass 的事。所以这个 pass 和 BlinnPhongOpaquePass 谁先谁后不影响
// 正确性（都在 Opaque stage，深度测试兜住遮挡关系）。
class UnlitOpaquePass final : public LinearPass {
public:
    UnlitOpaquePass();
    ~UnlitOpaquePass() override;

    std::string_view name() const noexcept override { return "UnlitOpaque"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
