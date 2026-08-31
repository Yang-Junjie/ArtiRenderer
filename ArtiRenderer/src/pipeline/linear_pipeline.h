#pragma once
#include "artichoco/renderer/render_pass.h"
#include "environment_resources.h"
#include "gbuffer_targets.h"
#include "linear_pass.h"
#include "linear_stage.h"
#include "pipeline.h"
#include "render_target_set.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace arti::rendering {

// 整条链作为**一个** renderer::RenderPass 提交给 RenderDevice::renderFrame，而不是每个 pass 包一层
// adapter。这样 RenderTargetSet::prepare() 有了确定位置 —— 在所有 pass 的 prepare() 之前，所以
// pass 里拿到的目标一定建好了，不依赖 prepare 的相对顺序。
class LinearPipeline final : public Pipeline, public arti::renderer::RenderPass {
public:
    explicit LinearPipeline(arti::renderer::RenderDevice& device);
    ~LinearPipeline() override;

    std::string_view name() const noexcept override { return "LinearPipeline"; }

    // stage 必须单调不减，否则抛。不排序 —— 执行顺序就是这里的调用顺序。
    void addPass(LinearStage stage, std::unique_ptr<LinearPass> pass);

    void render(FrameContext& frame) override;

    PickingPass* pickingPass() noexcept override { return m_picking_pass; }

    // renderer::RenderPass
    void prepare(arti::renderer::RenderPassPrepareContext& context) override;
    void record(arti::renderer::RenderPassContext& context) override;

private:
    struct Entry {
        LinearStage stage{ LinearStage::GBuffer };
        std::unique_ptr<LinearPass> pass;
        // "<stage> / <pass name>"，addPass 时算好，录制时零分配。
        std::string marker_label;
    };

    arti::renderer::RenderDevice* m_device{ nullptr };
    std::vector<Entry> m_passes;
    RenderTargetSet m_targets;
    // 和 m_targets 同一个角色，都是管线拥有、跨 pass 共享的具名资源。
    // G-Buffer 阶段的 pass 写 m_gbuffer，DeferredLightingPass 读；EnvironmentBakePass 写
    // m_environment，光照和天空读。
    GBufferTargets m_gbuffer;
    EnvironmentResources m_environment;
    // 借用指针，所有权在 m_passes 里。装配时记下来，省得每次取结果都去遍历找。
    PickingPass* m_picking_pass{ nullptr };
    // render() 每帧设好，prepare()/record() 回调里用。
    FrameContext* m_frame{ nullptr };
};

} // namespace arti::rendering
