#include "clear_scene_pass.h"

namespace arti::rendering {

void ClearScenePass::record(PassRecordContext& context) {
    auto& targets = context.targets();
    auto& gbuffer = context.gbuffer();
    auto& commands = context.commands();

    // 深度清成 1.0（远平面）。没有 stencil，所以最后两个参数是 false / 0。
    // DeferredLightingPass 就是靠「深度还是 1.0」判断这个像素是背景，所以这个值同时是
    // 「远平面」和「这里没有几何体」两个意思 —— 改它要连着改那边的判断。
    commands.clearDepthStencilTexture(&targets.sceneDepth(), nvrhi::AllSubresources, true, 1.0f,
            false, 0);

    // G-Buffer 全清成 0。严格说没有必要 —— 光照 pass 只读深度通过的像素，那些一定被几何体
    // 写过了。清它是为了可调试性：未初始化的 G-Buffer 在抓帧和调试视图里看到的是上一帧的残留
    // 或者显存垃圾，排查起来很费时间。真要省这三次全屏写入时，删的是下面这三行。
    const nvrhi::Color zero{ 0.0f, 0.0f, 0.0f, 0.0f };
    commands.clearTextureFloat(&gbuffer.albedoMetallic(), nvrhi::AllSubresources, zero);
    commands.clearTextureFloat(&gbuffer.normalRoughness(), nvrhi::AllSubresources, zero);
    commands.clearTextureFloat(&gbuffer.emissiveOcclusion(), nvrhi::AllSubresources, zero);

    // SceneColor 不在这里清，见头文件。
}

} // namespace arti::rendering
