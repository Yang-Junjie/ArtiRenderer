#include "linear_pipeline.h"

#include "artichoco/renderer/render_device.h"
#include "log.h"
#include "passes/forward_opaque_pass.h"
#include "passes/present_pass.h"

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
    m_targets.prepare(context.device(), context.framebuffer());

    for (const auto& entry: m_passes) {
        if (!entry.pass->isEnabled(*m_frame)) {
            continue;
        }
        PassPrepareContext pass_context{ context, *m_frame, m_targets };
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
        PassRecordContext pass_context{ context, *m_frame, m_targets };
        entry.pass->record(pass_context);
        commands.endMarker();
    }
}

std::unique_ptr<Pipeline> createForwardPipeline(arti::renderer::RenderDevice& device) {
    auto pipeline = std::make_unique<LinearPipeline>(device);

    // 两个 pass 之间不再互相认识：SceneColor 通过 RenderTargetSet 交接，顺序由 stage 表达。
    pipeline->addPass(LinearStage::Opaque, std::make_unique<ForwardOpaquePass>());
    pipeline->addPass(LinearStage::Output, std::make_unique<PresentPass>());

    getLogChannel().info("Created forward pipeline (Opaque -> Output)");
    return pipeline;
}

} // namespace arti::rendering
