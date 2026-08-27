#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 前向不透明 pass：把 DrawItem 画到离屏的 SceneColor / SceneDepth，
// 并登记到黑板供 PresentPass 消费。
//
// 目前只实现 unlit 着色（base_color * base_color_texture）。
// RenderScene::lights 还没有被消费。
class ForwardOpaquePass final : public LinearPass {
public:
    ForwardOpaquePass();
    ~ForwardOpaquePass() override;

    std::string_view name() const noexcept override { return "ForwardOpaque"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
