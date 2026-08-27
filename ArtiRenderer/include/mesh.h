#pragma once

#include "aabb.h"

#include <cstdint>

#include <vector>

namespace arti::rendering {

struct MeshVertex {
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec3 tangent{ 1.0f, 0.0f, 0.0f };
    glm::vec3 bitangent{ 0.0f, 0.0f, 1.0f };
    glm::vec2 uv{ 0.0f };
};

struct Submesh {
    uint32_t index_offset{ 0 };
    uint32_t index_count{ 0 };
    uint32_t vertex_offset{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t material_index{ 0 };
};

struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
    AABB bounds;
};

} // namespace arti::rendering
