#pragma once

#include <cstdint>

namespace arti::rendering {

// pass 的语义分类和装配点。执行顺序仍然是 addPass 的顺序，stage 不参与排序 —— 它的作用是让
// 「谁必须排在谁前面」变成可机检的声明，装错位置在安装时就抛，而不是等画面不对再查。
//
// Clear 独立成一个 stage（而不是让第一个绘制 pass 顺手清屏）是分 pass 的必要配套：几何
// 写入按 G-Buffer 编码拆成多个 pass 之后，「谁负责清屏」如果靠约定就成了隐式耦合 ——
// 改动 pass 顺序会静默地改变清屏行为。
enum class LinearStage : uint8_t {
    // 纯 compute，不碰任何 framebuffer：IBL 烘焙就在这里。排在最前面是因为它的产物
    // （irradiance / prefiltered / BRDF LUT）是 Lighting 的输入，而它自己不依赖任何渲染目标。
    EnvironmentBake,
    // 方向光的级联阴影深度图。和 EnvironmentBake 同一个性质：产物是 Lighting 的输入，自己
    // 不依赖任何场景渲染目标（阴影图分辨率固定，和场景分辨率无关）。
    //
    // 排在 Clear 之前而不是之后：这样「谁清场景目标」仍然只有 ClearScenePass 一处。阴影图
    // 自己的深度由 ShadowPass 自己清 —— 它只在有投影光源的帧才有内容，让 ClearScenePass
    // 去管它就成了隐式耦合。
    //
    // 它要重画一遍几何（每级一遍），所以在几何 pass 里算「排最前面的那个」；但它不写
    // G-Buffer、不写 SceneDepth，和 GBuffer 阶段没有资源交接。
    Shadow,
    Clear,
    // 几何写入：把材质属性编码进 G-Buffer + SceneDepth，一行光照都不算。
    // 拆 pass 的依据是 **G-Buffer 编码**而不是材质类型 —— 延迟管线里着色模型已经统一到
    // Lighting 里去了，同一种编码再分成几个 pass 只是几份一样的 PSO。
    GBuffer,
    // 延迟光照：全屏三角形读 G-Buffer + 深度，写 SceneColor。整条管线里唯一求值 BRDF 的地方。
    Lighting,
    // 天空排在 Lighting 之后而不是之前：深度测试 LessOrEqual + 不写深度，只填没被物体覆盖的
    // 像素。反过来先画天空的话，每个被物体挡住的像素都白画一遍。
    Sky,
    // 拾取排在 Sky 之后：它复用 GBuffer 阶段写好的深度做 LessOrEqual 测试，
    // 所以只有屏幕上可见的片元会写下 ID —— 「点到的」和「看到的」因此永远一致。
    // 排在 PostProcess 之前是因为它读的是深度、跟颜色无关，而读回是异步的：早点提交早点能取。
    Picking,
    // 场景侧后处理：读 SceneColor（场景线性 HDR），写 DisplayColor（显示线性 LDR）。
    // 必须排在 Lighting / Sky 之后（要读完整的场景）、Output 之前（PresentPass 贴的是
    // DisplayColor）。
    PostProcess,
    // 调试绘制：画在 DisplayColor 上（tone mapping **之后**），深度测试复用 SceneDepth。
    // 排在 PostProcess 之后是刻意的 —— 调试线的颜色不该被曝光和 tone 曲线改掉，
    // 「我给的颜色就是我看到的颜色」是它的全部意义。排在 Output 之前，所以两条呈现路径
    // （PresentPass 贴 backbuffer / ImGui 采纹理）看到的都是带调试线的画面。
    DebugOverlay,
    Output,
    // UI 排在 Output 之后，直接画进 backbuffer 而不是 SceneColor：UI 因此永远是原生分辨率，
    // 也不受场景侧后处理（tone mapping、缩放渲染）的影响。代价是 UI 不参与那些变换 ——
    // 对调试 UI 来说这正是想要的。
    UI,
};

// 稳定的 stage 名字，会进 GPU marker 标签，所以改名会改变 capture 里看到的东西。
constexpr const char* linearStageName(LinearStage stage) noexcept {
    switch (stage) {
        case LinearStage::EnvironmentBake:
            return "EnvironmentBake";
        case LinearStage::Shadow:
            return "Shadow";
        case LinearStage::Clear:
            return "Clear";
        case LinearStage::GBuffer:
            return "GBuffer";
        case LinearStage::Lighting:
            return "Lighting";
        case LinearStage::Sky:
            return "Sky";
        case LinearStage::Picking:
            return "Picking";
        case LinearStage::PostProcess:
            return "PostProcess";
        case LinearStage::DebugOverlay:
            return "DebugOverlay";
        case LinearStage::Output:
            return "Output";
        case LinearStage::UI:
            return "UI";
    }
    return "Unknown";
}

} // namespace arti::rendering
