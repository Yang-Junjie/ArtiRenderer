#pragma once
#include "render_scene.h"
#include "shadow_targets.h"

#include <array>

#include <glm/mat4x4.hpp>

namespace arti::rendering::detail {

// 按相机视锥和方向光算出四级 cascade。
//
// 关键取舍（都在 .cpp 里逐条注释）：
//   - 拟合**相机视锥**而不是场景包围盒（后者透视走样更严重）
//   - 每级用**包围球**定正交范围，而不是视锥角的 AABB —— 这样范围大小不随相机朝向变化，
//     texel 取整才真的稳定
//   - 正交范围按 texel 取整，消 shimmering
//   - near / far 只看「XY 上和这一级重叠的那些 draw」，比拿整个场景 AABB 紧得多
//
// light 必须是方向光。draws 用来收集投影体的深度范围，空列表时返回的 cascade 仍然合法
// （只是范围退化），调用方应该在没有 draw 的帧直接跳过整个 pass。
struct ShadowCascadeResult {
    std::array<ShadowCascade, kShadowCascadeCount> cascades{};
    float shadow_distance{ 0.0f };
};

ShadowCascadeResult computeShadowCascades(const RenderView& view, const LightDesc& light,
        const std::vector<DrawItem>& draws);

} // namespace arti::rendering::detail
