#pragma once
#include "linear_pass.h"
#include "pipeline.h"

#include <memory>
#include <string_view>
#include <vector>

namespace arti::rendering {

class LinearPipeline final : public Pipeline {
public:
    explicit LinearPipeline(arti::renderer::RenderDevice& device);
    ~LinearPipeline() override;

     std::string_view name() const noexcept override { return "LinearPipeline"; }

    void addPass(std::unique_ptr<LinearPass> pass);

    void render(FrameContext& frame) override;

private:
    // 把 LinearPass 适配成 ArtiChoco 的 renderer::RenderPass。
    class PassAdapter;

    arti::renderer::RenderDevice* m_device{ nullptr };
    std::vector<std::unique_ptr<LinearPass>> m_passes;
    std::vector<std::unique_ptr<PassAdapter>> m_adapters;
    // 复用，避免每帧分配。
    std::vector<arti::renderer::RenderPass*> m_submit_list;
};

} // namespace arti::rendering
