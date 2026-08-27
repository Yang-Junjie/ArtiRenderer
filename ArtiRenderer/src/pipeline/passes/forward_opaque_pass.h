#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 前向不透明 pass：把 DrawItem 画到 RenderTargetSet 的 SceneColor / SceneDepth。
//
// 离屏目标由管线的 RenderTargetSet 拥有，这个 pass 只是往里画，不再自己建、也不再对外暴露。
// 尺寸跟随由 RenderTargetSet 负责。
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
