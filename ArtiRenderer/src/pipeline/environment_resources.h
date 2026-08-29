#pragma once

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace arti::rendering {

// IBL 的烘焙产物，由 EnvironmentBakePass 写、PbrOpaquePass 和 SkyPass 读。
//
// 为什么不塞进 RenderTargetSet：那个类的名字承载的是色彩空间契约（scene 线性 / display 线性 /
// backbuffer），而且它刻意不是泛型 slot 机制。这几张图不是 render target，是 compute 的产物。
// 所以照 RenderTargetSet 解决「pass 之间怎么交接资源」的形状，另起一个同样具名的结构 ——
// pass 依然互不认识，顺序由 LinearStage 表达（EnvironmentBake < Opaque，装错在 addPass 就抛）。
struct EnvironmentResources {
    // 等距柱状源图烘出来的全景 cube，带完整 mip 链。天空直接采它，prefilter 也拿它当输入。
    nvrhi::TextureHandle environment;
    // 余弦卷积的漫反射辐照度，很小（32²）—— 它本来就只剩低频。
    nvrhi::TextureHandle irradiance;
    // GGX 预滤波的镜面反射，mip 层级对应 roughness。
    nvrhi::TextureHandle prefiltered;
    // split-sum 的 BRDF 积分项，和环境贴图无关，所以全局只烘一次。
    nvrhi::TextureHandle brdf_lut;
    nvrhi::SamplerHandle sampler;

    // prefiltered 的 mip 数。着色端要用它把 roughness 映射到 mip，硬编码的话改尺寸就错。
    uint32_t prefiltered_mips{ 0 };

    // false 表示这一帧没有可用的 IBL（没开环境、没填贴图、或者还没烘完）。此时上面那几个句柄
    // 指向 1×1 的黑色兜底资源 —— **不是空句柄**，否则 binding set 建不起来。
    // 着色端看到 false 就回落到常数环境项。
    bool ready{ false };

    // 内容换了才自增。下游靠它判断要不要重建 binding set，比自己存纹理指针再比对更明确
    // —— 和 RenderTargetSet::revision() 同一个套路。
    uint64_t revision{ 0 };
};

} // namespace arti::rendering
