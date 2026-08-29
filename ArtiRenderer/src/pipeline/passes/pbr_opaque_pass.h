#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 画 MaterialType::PBR 的不透明物体：metallic-roughness 工作流，GGX + Smith + Schlick，
// 一个方向光的直接光照，加一个常数环境项占位（IBL 落地后替换）。
//
// 每种材质一个 pass、一个 PSO —— 材质类型由 PSO 表达，着色器里不再有分支开关。
//
// 绑定集按 MaterialHandle 缓存而不是按纹理句柄（Blinn-Phong 那样）：PBR 一个 draw 要五张贴图，
// 按纹理组合做 key 是个五元组，而材质本来就是这个组合的天然身份。
class PbrOpaquePass final : public LinearPass {
public:
    PbrOpaquePass();
    ~PbrOpaquePass() override;

    std::string_view name() const noexcept override { return "PbrOpaque"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
