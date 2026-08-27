#pragma once

#include <cstdint>

namespace arti::rendering {

// pass 的语义分类和装配点。执行顺序仍然是 addPass 的顺序，stage 不参与排序 —— 它的作用是让
// 「谁必须排在谁前面」变成可机检的声明，装错位置在安装时就抛，而不是等画面不对再查。
//
// Clear 独立成一个 stage（而不是让第一个绘制 pass 顺手清屏）是分 pass 的必要配套：不透明
// 材质按类型拆成多个 pass 之后，「谁负责清屏」如果靠约定就成了隐式耦合 —— 改动 pass 顺序会
// 静默地改变清屏行为。
//
// 将来加 compute（比如 IBL 烘焙）就在这里插一个值，顺序约束立刻生效，不用改任何已有 pass。
enum class LinearStage : uint8_t {
    Clear,
    Opaque,
    Output,
    // UI 排在 Output 之后，直接画进 backbuffer 而不是 SceneColor：UI 因此永远是原生分辨率，
    // 也不受将来场景侧后处理（tone mapping、缩放渲染）的影响。代价是 UI 不参与那些变换 ——
    // 对调试 UI 来说这正是想要的。
    UI,
};

// 稳定的 stage 名字，会进 GPU marker 标签，所以改名会改变 capture 里看到的东西。
constexpr const char* linearStageName(LinearStage stage) noexcept {
    switch (stage) {
        case LinearStage::Clear:
            return "Clear";
        case LinearStage::Opaque:
            return "Opaque";
        case LinearStage::Output:
            return "Output";
        case LinearStage::UI:
            return "UI";
    }
    return "Unknown";
}

} // namespace arti::rendering
