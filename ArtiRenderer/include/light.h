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
};
} // namespace arti::rendering