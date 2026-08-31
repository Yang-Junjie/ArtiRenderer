#pragma once

#include <glm/glm.hpp>

namespace arti::rendering {

// 一条世界空间的调试线段。
//
// 颜色是**显示线性**的，不是场景线性：调试线画进 DisplayColor（tone mapping 之后那一层），
// 所以写进去的值就是最终看到的颜色。这正是调试绘制想要的 —— 用红/绿/蓝表示不同含义时，
// 不希望曝光和 tone 曲线把它们改掉。alpha 目前不参与混合，留着是为了以后加半透明调试面。
//
// 没有线宽：Vulkan 只在 wideLines 特性下支持 >1 像素的线，nvrhi 的 RasterState 也没有暴露
// 这一项。真要粗线得把线段展开成四边形（一条线两个三角形），那是另一件事。
struct DebugLine {
    glm::vec3 from{ 0.0f };
    glm::vec3 to{ 0.0f };
    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

} // namespace arti::rendering
