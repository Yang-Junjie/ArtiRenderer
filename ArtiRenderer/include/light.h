#pragma once
#include "glm/glm.hpp"
namespace arti::rendering {
enum class LightType {
    Directional,
    Point,
    Spot,
};
struct LightDesc {
    LightType type{ LightType::Directional };

    glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    float range{ 10.0f };
    float inner_cone_radians{ 0.35f };
    float outer_cone_radians{ 0.5f };
    bool enabled{ true };

    // 下面两个目前只有**方向光**读（点光 / 聚光的阴影是另一件事，需要 cubemap 和透视投影）。
    bool casts_shadow{ true };
    // 阴影覆盖到多远。级联阴影拟合相机视锥，必须给一个远端截断 —— 否则远平面很远的
    // 场景会把整张阴影图摊给几百米，近处一个 texel 能盖好几厘米。
    //
    // 三家主流引擎都有这个旋钮（Godot 的 max_distance、UE 的 CSM distance、Unity 的 shadow
    // distance），默认值也拄 Godot 的 100。
    float shadow_distance{ 100.0f };
};
} // namespace arti::rendering