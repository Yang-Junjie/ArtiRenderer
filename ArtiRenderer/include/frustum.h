#pragma once

#include "aabb.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>

namespace arti::rendering {

// 视锥的六个平面，法线一律**朝内**（内侧点代入平面方程得正值）。
//
// 从 view-projection 直接取行向量组合（Gribb-Hartmann），不走「反解八个角再叉积」——
// 后者要多算一个逆矩阵，而且角点顺序写错时的症状是「某个方向剔多了」，很难查。
struct Frustum {
    // 顺序：左 右 下 上 近 远。索引不对外承诺含义，intersects() 是唯一的正经用法。
    std::array<glm::vec4, 6> planes{};

    Frustum() = default;

    // **这里的深度约定是 ZO（z ∈ [0,1]），不是 NO（z ∈ [-1,1]）。**
    //
    // 本工程全程显式用 glm 的 _ZO 变体（perspectiveRH_ZO / orthoRH_ZO），并且
    // **没有**定义 GLM_FORCE_DEPTH_ZERO_TO_ONE —— 靠的就是这些后缀。
    //
    // 两种约定只在近平面上不同：
    //
    //   约定                    近平面        远平面
    //   NO（OpenGL 默认）       row3 + row2   row3 - row2
    //   ZO（本工程）            row2          row3 - row2
    //
    // 写成 NO 那条公式**不会崩、不会报错**，而且两种矩阵下的症状差别很大 ——
    // 这一点是实测出来的，不是推的：
    //
    //   透视（perspectiveRH_ZO，near=0.1 far=100）：
    //     ZO  row2        归一化后 (0,0,-1,-0.1)   → 近平面 z=-0.1，正确
    //     NO  row3+row2   归一化后 (0,0,-1,-0.05)  → 近平面 z=-0.05
    //     两个都朝前、都能剔掉背后的东西。差别只是近处多留一条 0.05 的缝，**看不出来**。
    //
    //   正交（orthoRH_ZO，row3 = (0,0,0,1)，near=1 far=100）：
    //     ZO  row2        → z <= -1，正确
    //     NO  row3+row2   → z <= 98，**几乎什么都不剔**
    //
    // 所以「NO 公式导致剔除失效」这件事是**正交**特有的，而正交正是阴影 cascade 用的
    // （shadow_cascades.cpp 的 orthoRH_ZO）。frustum_test 里的正交用例专治这个；
    // 只有透视用例的话，这个 bug 会一路溜到阴影那边才发作。
    static Frustum fromViewProjection(const glm::mat4& view_projection) noexcept
    {
        // glm 是列主序：view_projection[col][row]。平面提取要的是**行**向量。
        const auto row = [&view_projection](int index) {
            return glm::vec4{
                view_projection[0][index],
                view_projection[1][index],
                view_projection[2][index],
                view_projection[3][index],
            };
        };

        const glm::vec4 row0 = row(0);
        const glm::vec4 row1 = row(1);
        const glm::vec4 row2 = row(2);
        const glm::vec4 row3 = row(3);

        Frustum frustum;
        frustum.planes[0] = row3 + row0; // 左
        frustum.planes[1] = row3 - row0; // 右
        frustum.planes[2] = row3 + row1; // 下
        frustum.planes[3] = row3 - row1; // 上
        frustum.planes[4] = row2;        // 近 —— ZO 约定，见上面那张表
        frustum.planes[5] = row3 - row2; // 远

        // 归一化：不做的话「盒子到平面的距离」这个量纲不对，保守测试会按平面缩放偏。
        for (auto& plane: frustum.planes) {
            const float length = glm::length(glm::vec3{ plane });
            if (length > 0.0f) {
                plane /= length;
            }
        }
        return frustum;
    }

    // 保守测试：**部分相交算可见**。横跨近平面的大盒子必须判可见，否则站在墙里会看穿墙。
    //
    // 判据是「AABB 在这个平面法线方向上最靠内的那个角」——它都在外侧，整个盒子就在外侧。
    // 只要有一个平面能把盒子整个排除，就不可见。
    //
    // 这是标准的保守做法，会漏掉一类情形：盒子同时跨过好几个平面的外侧、但并不真和视锥相交
    // （视锥角落附近）。那种误判是「多画一个」，代价可以接受；反过来（少画）是画面错误。
    bool intersects(const AABB& box) const noexcept
    {
        // 空盒约定为不可见。默认构造的 AABB 是空的（min > max），当可见会让它参与后续
        // 所有平面测试并给出无意义的结果。
        if (box.isEmpty()) {
            return false;
        }

        for (const auto& plane: planes) {
            const glm::vec3 normal{ plane };
            // 法线为零的平面（默认构造的 Frustum）跳过 —— 那种 Frustum 不剔任何东西，
            // 忘了初始化时的症状是「没有剔除」而不是「画面全黑」。
            if (normal == glm::vec3{ 0.0f }) {
                continue;
            }
            const glm::vec3 innermost{
                normal.x >= 0.0f ? box.max.x : box.min.x,
                normal.y >= 0.0f ? box.max.y : box.min.y,
                normal.z >= 0.0f ? box.max.z : box.min.z,
            };
            if (glm::dot(normal, innermost) + plane.w < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace arti::rendering
