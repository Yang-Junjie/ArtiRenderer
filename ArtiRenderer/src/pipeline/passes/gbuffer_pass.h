#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把 MaterialType::PBR 的表面写进 G-Buffer：metallic-roughness 的几何 + 材质属性，
// 一行光照都不算。通道分配见 gbuffer.slang 顶部。
//
// 延迟管线里几何 pass 的划分依据是 **G-Buffer 编码**而不是材质类型 —— 着色模型已经统一到
// DeferredLightingPass 里去了。所以现在只需要这一个 pass：再加一个的理由应该是「它写的是
// 另一套编码」（透明、蒙皮、地形），而不是「它是另一种材质」。
//
// 绑定集按 MaterialHandle 缓存而不是按纹理句柄：一个 draw 要五张贴图，按纹理组合做 key 是个
// 五元组，而材质本来就是这个组合的天然身份。
class GBufferPass final : public LinearPass {
public:
    GBufferPass();
    ~GBufferPass() override;

    std::string_view name() const noexcept override { return "GBuffer"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
