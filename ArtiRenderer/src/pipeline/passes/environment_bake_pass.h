#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 把 RenderScene::environment 的等距柱状 HDR 烘成 IBL 三件套，产物放进 EnvironmentResources：
//   environment  512², 10 mips —— 天空直接采它，prefilter 拿它当输入
//   irradiance    32², 1 mip   —— 余弦卷积的漫反射辐照度
//   prefiltered  128², 8 mips  —— GGX 预滤波，mip 对应 roughness
//   brdf_lut     256², 2D      —— split-sum 的积分项，与环境无关，全局只烘一次
//
// 全 compute，不碰任何 framebuffer，所以装在最前面的 EnvironmentBake 阶段。
//
// 按 equirectangular_texture 的句柄缓存：换贴图才重烘。改 intensity **不会**重烘 ——
// 那是着色时乘上去的倍率，不参与卷积。
class EnvironmentBakePass final : public LinearPass {
public:
    EnvironmentBakePass();
    ~EnvironmentBakePass() override;

    std::string_view name() const noexcept override { return "EnvironmentBake"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
