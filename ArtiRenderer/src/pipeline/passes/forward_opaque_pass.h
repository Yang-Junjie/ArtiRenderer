#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 前向不透明 pass：把 DrawItem 画到自己持有的离屏 SceneColor / SceneDepth。
//
// 离屏纹理由这个 pass 拥有（nvrhi::TextureHandle 引用计数），下游 pass 通过
// sceneColor()/sceneDepth() 借用。尺寸跟着 backbuffer 变，重建时纹理指针会变。
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

    // 只有在 prepare() 跑过之后才可用，否则抛 logic_error。
    nvrhi::ITexture& sceneColor() const;
    nvrhi::ITexture& sceneDepth() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
