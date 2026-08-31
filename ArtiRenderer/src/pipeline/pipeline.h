#pragma once

#include <memory>
#include <string_view>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {

class FrameContext;


class PickingPass;

class Pipeline {
public:
    virtual ~Pipeline() = default;

    virtual std::string_view name() const noexcept = 0;

    virtual void render(FrameContext& frame) = 0;

    // 拾取结果要跨帧取（读回是异步的），而 pass 归管线所有，所以由管线交出来。
    // 没装 PickingPass 的管线返回 nullptr。
    virtual PickingPass* pickingPass() noexcept { return nullptr; }
};

std::unique_ptr<Pipeline> createDeferredPipeline(arti::renderer::RenderDevice& device);

} // namespace arti::rendering
