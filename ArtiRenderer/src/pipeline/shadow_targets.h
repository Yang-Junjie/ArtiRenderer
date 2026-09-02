#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>

#include <glm/mat4x4.hpp>

namespace arti::rendering {

// 方向光级联阴影的级数。四级是主流默认（Godot 的 SHADOW_PARALLEL_4_SPLITS 就是默认值），
// 着色端要按它展开循环，所以是编译期常量而不是运行期参数。
inline constexpr uint32_t kShadowCascadeCount = 4;

// 每一级的边长。写死而不是做成配置项：它和级数一样属于「质量档位」，而现在没有画质档位系统
// 可以挂靠 —— 暴露出去就要序列化、Inspector 和取值校验，收益不抵成本。
inline constexpr uint32_t kShadowMapResolution = 2048;

// 一级 cascade 的全部结果。ShadowPass 每帧算好塞进 ShadowTargets，DeferredLightingPass 读。
struct ShadowCascade {
    // 世界空间 -> 这一级的光源裁剪空间。渲深度图和采样都用它。
    glm::mat4 light_view_projection{ 1.0f };
    // 这一级覆盖到相机前方多远（view-space 距离）。着色端按它选级。
    float split_far{ 0.0f };
    // 一个 texel 在这一级覆盖多少世界单位。PCF 的采样步长要它。
    float world_units_per_texel{ 0.0f };
};

// 级联阴影的深度图。ShadowPass 写，DeferredLightingPass 读。
//
// 为什么不塞进 RenderTargetSet：那个类的名字承载的是**色彩空间**契约（scene 线性 /
// display 线性 / backbuffer），而这里躺的是深度。照 GBufferTargets / EnvironmentResources 的
// 先例另起一个同样具名的结构：pass 依然互不认识，顺序由 LinearStage 表达
// （Shadow < Lighting，装错在 addPass 就抛）。
//
// 形状是**一张 Texture2DArray + 每级一个 framebuffer**，不是 atlas（一张大图切 N 块）：
// array 让每级的 UV 计算完全一样，只多一个 slice 下标；atlas 要在着色端为每级做偏移和缩放，
// 还得防跨块采样漏到邻居。Unity 用 atlas 是因为它要把 directional / spot / point 混在一起
// 按需分配，而这里只有方向光一个消费者。
//
// 和 GBufferTargets 的关键区别：**尺寸是固定的，不跟 RenderTargetSet 的 revision 走**。
// 阴影图分辨率和场景渲染分辨率无关，所以这个类建一次就够，别照抄那边的 bound_revision 逻辑。
class ShadowTargets {
public:
    ShadowTargets() = default;

    ShadowTargets(const ShadowTargets&) = delete;
    ShadowTargets& operator=(const ShadowTargets&) = delete;

    // 幂等：已经建好就直接返回。每帧在所有 pass 的 prepare() 之前调。
    void prepare(nvrhi::IDevice& device);

    bool isReady() const noexcept { return bool(m_depth_array); }

    // 重建时自增。DeferredLightingPass 靠它判断要不要重建 binding set —— 和
    // RenderTargetSet::revision() / GBufferTargets::revision() 同一个套路。
    uint64_t revision() const noexcept { return m_revision; }

    // 整个 array，采样侧用（Texture2DArray）。
    nvrhi::ITexture& depthArray() const;

    // 第 cascade 级的 framebuffer，渲染侧用。越界抛。
    nvrhi::IFramebuffer& framebuffer(uint32_t cascade) const;

    // 这一帧的 cascade 参数。ShadowPass 真的渲了才设；prepare() 每帧先清掉，
    // 所以「这一帧没阴影」和「上一帧的参数残留」不会混。
    void setCascades(const std::array<ShadowCascade, kShadowCascadeCount>& cascades,
            float shadow_distance) noexcept;
    bool hasCascades() const noexcept { return m_has_cascades; }
    const std::array<ShadowCascade, kShadowCascadeCount>& cascades() const noexcept {
        return m_cascades;
    }
    // 阴影整体覆盖到多远，等于 min(相机远平面, light.shadow_distance)。
    float shadowDistance() const noexcept { return m_shadow_distance; }

private:
    nvrhi::TextureHandle m_depth_array;
    std::array<nvrhi::FramebufferHandle, kShadowCascadeCount> m_framebuffers;
    uint64_t m_revision{ 0 };

    std::array<ShadowCascade, kShadowCascadeCount> m_cascades{};
    float m_shadow_distance{ 0.0f };
    bool m_has_cascades{ false };
};

} // namespace arti::rendering
