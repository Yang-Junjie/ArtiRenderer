#pragma once

#include "render_target_set.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace arti::rendering {

// 延迟管线的 G-Buffer。GBufferPass 写，DeferredLightingPass 读。
//
// 为什么不塞进 RenderTargetSet：那个类的名字承载的是**色彩空间**契约（scene 线性 /
// display 线性 / backbuffer），而 G-Buffer 里躺的是材质属性 —— 法线和 roughness 谈不上什么
// 色彩空间。所以照 EnvironmentResources 的先例另起一个同样具名的结构：pass 依然互不认识，
// 顺序由 LinearStage 表达（GBuffer < Lighting，装错在 addPass 就抛）。
//
// 深度不在这里：它是 RenderTargetSet 的 SceneDepth。G-Buffer 只是**借**它当深度附件，
// DeferredLightingPass 随后把它当 SRV 采样反投影出世界坐标 —— 一张深度两个用途，所以它归
// RenderTargetSet 管而不是归这里。代价是这个类要跟着 RenderTargetSet 的生命周期走，
// 所以 prepare() 直接吃它，并用它的 revision() 当重建触发条件。
//
// PickingPass **不**借这张：它要做的是「深度相等」的比较，而那要求两个 pass 的顶点变换
// 逐位一致，靠不住。它自己带一张深度，见 picking_pass.cpp。
//
// 通道分配（写入侧 gbuffer.slang / 读取侧 deferred_lighting.slang 必须和这里逐通道对齐）：
//   albedo_metallic     SRGBA8_UNORM   rgb = albedo（硬件做 sRGB 编码），a = metallic
//   normal_roughness    RGBA16_FLOAT   xyz = 世界法线，w = roughness
//   emissive_occlusion  RGBA16_FLOAT   rgb = emissive（场景线性 HDR），a = occlusion
class GBufferTargets {
public:
    GBufferTargets() = default;

    GBufferTargets(const GBufferTargets&) = delete;
    GBufferTargets& operator=(const GBufferTargets&) = delete;

    // 每帧在所有 pass 的 prepare() 之前调用，紧跟在 RenderTargetSet::prepare() 之后 ——
    // 尺寸和深度附件都从它来。targets 的 revision 没变就整个跳过。
    void prepare(nvrhi::IDevice& device, const RenderTargetSet& targets);

    bool isReady() const noexcept { return bool(m_framebuffer); }

    // 重建时自增。DeferredLightingPass 靠它判断要不要重建 binding set ——
    // 和 RenderTargetSet::revision() 同一个套路。
    uint64_t revision() const noexcept { return m_revision; }

    nvrhi::ITexture& albedoMetallic() const;
    nvrhi::ITexture& normalRoughness() const;
    nvrhi::ITexture& emissiveOcclusion() const;

    // 三张颜色附件 + SceneDepth。几何 pass 用它，光照 pass **不能**用 ——
    // 光照要把深度当 SRV 采，同一张图不能同时是深度附件。
    nvrhi::IFramebuffer& framebuffer() const;

private:
    nvrhi::TextureHandle m_albedo_metallic;
    nvrhi::TextureHandle m_normal_roughness;
    nvrhi::TextureHandle m_emissive_occlusion;
    nvrhi::FramebufferHandle m_framebuffer;
    // RenderTargetSet 的 revision 快照。它一变就说明离屏目标（含 SceneDepth）换了，
    // 附件失效，整套重建。初值刻意不是 0：RenderTargetSet 第一次 prepare 之后就是 1，
    // 但万一将来它从 0 开始，用 0 当初值会让第一帧误判成「没变」。
    uint64_t m_source_revision{ static_cast<uint64_t>(-1) };
    uint64_t m_revision{ 0 };
};

} // namespace arti::rendering
