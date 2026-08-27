#pragma once

#include <cstdint>

namespace arti::rendering {

// pass 的语义分类和装配点。执行顺序仍然是 addPass 的顺序，stage 不参与排序 —— 它的作用是让
// 「谁必须排在谁前面」变成可机检的声明，装错位置在安装时就抛，而不是等画面不对再查。
//
// 只有两个是刻意的：现在只有两个 pass。将来加 compute（比如 IBL 烘焙）就在这里插一个值，
// 顺序约束立刻生效，不用改任何已有 pass。
enum class LinearStage : uint8_t {
    Opaque,
    Output,
};

// 稳定的 stage 名字，会进 GPU marker 标签，所以改名会改变 capture 里看到的东西。
constexpr const char* linearStageName(LinearStage stage) noexcept {
    switch (stage) {
        case LinearStage::Opaque:
            return "Opaque";
        case LinearStage::Output:
            return "Output";
    }
    return "Unknown";
}

} // namespace arti::rendering
