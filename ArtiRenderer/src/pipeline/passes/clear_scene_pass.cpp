#include "clear_scene_pass.h"

namespace arti::rendering {

void ClearScenePass::record(PassRecordContext& context)
{
    auto& targets = context.targets();
    auto& commands = context.commands();

    const auto& clear = context.frame().scene().clear_color;
    commands.clearTextureFloat(&targets.sceneColor(), nvrhi::AllSubresources,
            nvrhi::Color{ clear.r, clear.g, clear.b, clear.a });
    // 深度清成 1.0（远平面）。没有 stencil，所以最后两个参数是 false / 0。
    commands.clearDepthStencilTexture(
            &targets.sceneDepth(), nvrhi::AllSubresources, true, 1.0f, false, 0);
}

} // namespace arti::rendering
