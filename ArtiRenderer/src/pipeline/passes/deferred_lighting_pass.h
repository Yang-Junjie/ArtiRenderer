#pragma once
#include "linear_pass.h"

#include <memory>

namespace arti::rendering {

// 延迟光照：一个覆盖全屏的三角形，读 G-Buffer + SceneDepth，把光算出来写进 SceneColor。
// 整条管线里唯一求值 BRDF 的地方 —— GBuffer 阶段只存材质属性。
//
// 光源走一个按需增长的 StructuredBuffer，着色器里逐光源循环，方向光 / 点光 / 聚光三种都在
// 同一个循环里（类型是每个光源自己的字段，不是三条代码路径）。这是延迟渲染真正的收益：
// 光源数和几何复杂度解耦，加一个灯的代价是每个**可见像素**多跑一次 BRDF。
//
// 没有光源剔除，每个像素遍历全部光源 —— 几个到几十个够用，上百个才需要 tile / cluster 分桶。
// 那一步加的是循环外面的一层索引，不用改着色本身。
//
// 光源缓冲按 frame slot 开数组（照 ImGuiPass / DebugLinePass 的形状），绑定集也跟着分 ——
// 缓冲是绑定集里的一个 StructuredBuffer SRV，不像顶点缓冲那样单独绑。
//
// 输出目标用 RenderTargetSet::sceneColorFramebuffer()（只挂 SceneColor、不挂深度）：世界坐标
// 要从深度反投影出来，也就是 SceneDepth 必须当 SRV 采，同一张图不能同时是深度附件
// —— Vulkan 的 feedback loop。所以「这个像素有没有几何体」是靠采回来的深度自己判断的。
//
// 这个 pass 是 SceneColor **每一个**像素的唯一写入者：全屏三角形、无深度测试、无 discard，
// 没有几何体的像素输出 RenderScene::clear_color。所以 ClearScenePass 不清 SceneColor。
// （不用 discard 是因为 Slang 会把它编成 OpDemoteToHelperInvocation，那要 Vulkan 1.3 的
// shaderDemoteToHelperInvocation 特性，当前设备没开；顺带也更省。）
//
// 走 graphics 而不是 compute：compute 要求 SceneColor 开 isUAV，而 RenderTargetSet 刻意没开
// （某些硬件会因此放弃 framebuffer 压缩，见那个文件的注释）。真做 tiled / clustered 着色时
// 这个取舍要重新算一次，那时改的是建纹理的地方。
//
// 常驻启用：没有它 SceneColor 里就只剩 clear_color，所有几何体都不会出现。
class DeferredLightingPass final : public LinearPass {
public:
    DeferredLightingPass();
    ~DeferredLightingPass() override;

    std::string_view name() const noexcept override { return "DeferredLighting"; }

    void prepare(PassPrepareContext& context) override;
    void record(PassRecordContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
