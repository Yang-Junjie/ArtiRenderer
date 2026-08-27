#pragma once

#include <memory>
#include <string_view>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {

class FrameContext;


class Pipeline {
public:
    virtual ~Pipeline() = default;

    virtual std::string_view name() const noexcept = 0;

    virtual void render(FrameContext& frame) = 0;
};

std::unique_ptr<Pipeline> createForwardPipeline(arti::renderer::RenderDevice& device);

} // namespace arti::rendering
