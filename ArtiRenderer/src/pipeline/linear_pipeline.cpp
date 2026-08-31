#include "linear_pipeline.h"

#include "artichoco/renderer/render_device.h"
#include "log.h"
#include "passes/clear_scene_pass.h"
#include "passes/deferred_lighting_pass.h"
#include "passes/environment_bake_pass.h"
#include "passes/gbuffer_pass.h"
#include "passes/imgui_pass.h"
#include "passes/picking_pass.h"
#include "passes/present_pass.h"
#include "passes/sky_pass.h"
#include "passes/tonemap_pass.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace arti::rendering {

LinearPipeline::LinearPipeline(arti::renderer::RenderDevice& device)
        : m_device(&device) {}

LinearPipeline::~LinearPipeline() = default;

void LinearPipeline::addPass(LinearStage stage, std::unique_ptr<LinearPass> pass) {
    if (!pass) {
        throw std::invalid_argument("A linear pipeline pass must not be null.");
    }
    // 只校验、不排序。装错位置在这里就抛，而不是等画面不对再查。
    if (!m_passes.empty() && stage < m_passes.back().stage) {
        throw std::invalid_argument("Linear pipeline stages must be installed in order.");
    }

    // 记下 PickingPass 的位置，Renderer 取拾取结果要用。
    if (auto* picking = dynamic_cast<PickingPass*>(pass.get())) {
        m_picking_pass = picking;
    }

    Entry entry;
    entry.marker_label =
            std::string{ linearStageName(stage) } + " / " + std::string{ pass->name() };
    entry.stage = stage;
    entry.pass = std::move(pass);
    m_passes.push_back(std::move(entry));
}

void LinearPipeline::render(FrameContext& frame) {
    m_frame = &frame;

    // 整条链只提交一个 RenderPass，所以 renderFrame 的「空列表要抛」这个边界不存在了。
    std::array<arti::renderer::RenderPass*, 1> submit{ this };
    const auto result = m_device->renderFrame(submit);
    frame.statistics().rendered = result.wasRendered();

    m_frame = nullptr;
}

void LinearPipeline::prepare(arti::renderer::RenderPassPrepareContext& context) {
    if (m_frame == nullptr) {
        throw std::logic_error("LinearPipeline::prepare() outside render().");
    }

    // 在所有 pass 的 prepare() 之前，所以 pass 拿到的目标一定是建好的。
    const auto& settings = m_frame->settings();
    m_targets.prepare(context.device(), context.framebuffer(), settings.scene_target_width,
            settings.scene_target_height);
    // G-Buffer 紧跟其后：它的尺寸和深度附件都从 m_targets 来，靠它的 revision 判断要不要重建。
    m_gbuffer.prepare(context.device(), m_targets);

    for (const auto& entry: m_passes) {
        if (!entry.pass->isEnabled(*m_frame)) {
            continue;
        }
        PassPrepareContext pass_context{ context, *m_frame, m_targets, m_gbuffer, m_environment };
        entry.pass->prepare(pass_context);
    }
}

void LinearPipeline::record(arti::renderer::RenderPassContext& context) {
    if (m_frame == nullptr) {
        throw std::logic_error("LinearPipeline::record() outside render().");
    }

    auto& commands = context.commands();
    for (const auto& entry: m_passes) {
        if (!entry.pass->isEnabled(*m_frame)) {
            continue;
        }
        commands.beginMarker(entry.marker_label.c_str());
        PassRecordContext pass_context{ context, *m_frame, m_targets, m_gbuffer, m_environment };
        entry.pass->record(pass_context);
        commands.endMarker();
    }
}

std::unique_ptr<Pipeline> createDeferredPipeline(arti::renderer::RenderDevice& device) {
    auto pipeline = std::make_unique<LinearPipeline>(device);

    // pass 之间不互相认识：G-Buffer 通过 GBufferTargets 交接、SceneColor 通过 RenderTargetSet
    // 交接，顺序由 stage 表达。
    //
    // 清屏独立成 pass，而不是让 GBufferPass 顺手清：这样再加几何 pass（透明、蒙皮、地形）
    // 时不用去动「谁排第一」这个隐式约束。
    // 几何写入拆 pass 的依据是 **G-Buffer 编码**，不是材质类型 —— 着色模型不由 pass 表达，
    // 整条管线只有 DeferredLightingPass 求值 BRDF。所以现在只需要一个几何 pass。
    //
    // IBL 烘焙排在最前面：它是纯 compute，产物是 Lighting 和 Sky 的输入，自己不依赖任何目标。
    // 按环境贴图句柄缓存，所以只有换贴图的那一帧真的烘。
    pipeline->addPass(LinearStage::EnvironmentBake, std::make_unique<EnvironmentBakePass>());
    pipeline->addPass(LinearStage::Clear, std::make_unique<ClearScenePass>());
    pipeline->addPass(LinearStage::GBuffer, std::make_unique<GBufferPass>());
    pipeline->addPass(LinearStage::Lighting, std::make_unique<DeferredLightingPass>());
    // 天空在光照之后：深度测试挡掉被物体覆盖的像素，省一遍 overdraw。
    pipeline->addPass(LinearStage::Sky, std::make_unique<SkyPass>());
    // 常驻安装但按需生效：没有拾取请求的帧 isEnabled() 是 false，ID 缓冲都不会建。
    pipeline->addPass(LinearStage::Picking, std::make_unique<PickingPass>());
    // 常驻启用：SceneColor 是 HDR，从这里开始下游看到的都是压好的 DisplayColor。
    // 两条呈现路径（PresentPass 贴 backbuffer / ImGui 采纹理）因此看到同一个画面。
    pipeline->addPass(LinearStage::PostProcess, std::make_unique<TonemapPass>());
    pipeline->addPass(LinearStage::Output, std::make_unique<PresentPass>());
    // 常驻安装，但没有 draw data 的帧里 isEnabled() 是 false —— 不用 UI 的运行时不付代价，
    // 也不用为了开关 UI 去换一条管线。
    pipeline->addPass(LinearStage::UI, std::make_unique<ImGuiPass>());

    getLogChannel().info("Created deferred pipeline (Bake -> Clear -> GBuffer -> Lighting -> "
                         "Sky -> Tonemap -> Output -> UI)");
    return pipeline;
}

} // namespace arti::rendering
