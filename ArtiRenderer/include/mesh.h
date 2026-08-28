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

// createMesh 之后还能查到的东西。CPU 侧的顶点数据上传完就丢了，所以想知道网格有多大只能问这里。
//
// bounds 是**局部空间、整个网格**的包围盒，不是单个 submesh 的 —— Submesh 目前不带自己的
// 包围盒。多 submesh 的网格因此拿到的是偏大的盒子，对视锥剔除是安全方向（宁可多画，不能漏画）。
// 真需要逐 submesh 精度时给 Submesh 加 bounds 字段，createMesh 里顺手算出来。
struct MeshInfo {
    AABB bounds;
    uint32_t submesh_count{ 0 };
    uint32_t vertex_count{ 0 };
    uint32_t index_count{ 0 };
};

} // namespace arti::rendering
