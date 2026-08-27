#include "linear_pipeline.h"

#include "artichoco/renderer/render_device.h"
#include "log.h"
#include "passes/forward_opaque_pass.h"
#include "passes/present_pass.h"

#include <stdexcept>
#include <utility>

namespace arti::rendering {

// FrameContext 每帧由 Renderer 重新构造，所以 adapter 每帧被 bind 一次。
class LinearPipeline::PassAdapter final : public arti::renderer::RenderPass {
public:
    explicit PassAdapter(LinearPass& pass) noexcept
        : m_pass(&pass)
    {}

    void bind(FrameContext& frame) noexcept
    {
        m_frame = &frame;
    }

    void prepare(arti::renderer::RenderPassPrepareContext& context) override
    {
        PassPrepareContext wrapped{ context, *m_frame };
        m_pass->prepare(wrapped);
    }

    void record(arti::renderer::RenderPassContext& context) override
    {
        PassRecordContext wrapped{ context, *m_frame };
        m_pass->record(wrapped);
    }

private:
    LinearPass* m_pass{ nullptr };
    FrameContext* m_frame{ nullptr };
};

LinearPipeline::LinearPipeline(arti::renderer::RenderDevice& device)
    : m_device(&device)
{}

LinearPipeline::~LinearPipeline() = default;

void LinearPipeline::addPass(std::unique_ptr<LinearPass> pass)
{
    if (!pass) {
        throw std::invalid_argument("A linear pipeline pass must not be null.");
    }
    m_adapters.push_back(std::make_unique<PassAdapter>(*pass));
    m_passes.push_back(std::move(pass));
}

void LinearPipeline::render(FrameContext& frame)
{
    m_submit_list.clear();
    m_submit_list.reserve(m_passes.size());
    for (size_t index = 0; index < m_passes.size(); ++index) {
        if (!m_passes[index]->isEnabled(frame)) {
            continue;
        }
        m_adapters[index]->bind(frame);
        m_submit_list.push_back(m_adapters[index].get());
    }

    // RenderDevice::renderFrame 对空 pass 列表会抛，这里提前退出。
    if (m_submit_list.empty()) {
        return;
    }

    const auto result = m_device->renderFrame(m_submit_list);
    frame.statistics().rendered = result.wasRendered();
}

std::unique_ptr<Pipeline> createForwardPipeline(arti::renderer::RenderDevice& device)
{
    auto pipeline = std::make_unique<LinearPipeline>(device);

    // PresentPass 直接读 ForwardOpaquePass 的离屏纹理，所以顺序不能颠倒。
    auto forward_opaque = std::make_unique<ForwardOpaquePass>();
    auto present = std::make_unique<PresentPass>(*forward_opaque);

    pipeline->addPass(std::move(forward_opaque));
    pipeline->addPass(std::move(present));

    getLogChannel().info("Created forward pipeline (ForwardOpaque -> Present)");
    return pipeline;
}

} // namespace arti::rendering
