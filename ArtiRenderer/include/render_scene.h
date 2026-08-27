#pragma once
#include "aabb.h"
#include "handle.h"
#include "light.h"

#include <cstdint>

#include <vector>

namespace arti::rendering {

struct RenderView {
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::vec3 camera_position{ 0.0f };
};

struct DrawItem {
    MeshHandle mesh;
    uint32_t submesh_index{ 0 };
    MaterialHandle material;
    glm::mat4 transform{ 1.0f };
    AABB world_bounds;
};

struct RenderScene {
    RenderView view;
    std::vector<DrawItem> draws;
    std::vector<LightDesc> lights;
    glm::vec4 clear_color{ 0.04f, 0.08f, 0.12f, 1.0f };
};

} // namespace arti::rendering
