#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering{

// 画 MaterialType::BlinnPhong 的不透明物体。消费 RenderScene::lights 里第一个启用的方向光。
//
// 不清屏 —— 那是 ClearScenePass 的事。
class BlinnPhongOpaquePass final : public LinearPass {
public:
    BlinnPhongOpaquePass();
    ~BlinnPhongOpaquePass() override;

    std::string_view name() const noexcept override { return "BlinnPhongOpaque"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
