#include "mesh_vertex_layout.h"

#include "mesh.h"

namespace arti::rendering::detail {
namespace {

arti::renderer::VertexBufferLayout makeMeshVertexLayout()
{
    using arti::renderer::VertexAttribute;
    using arti::renderer::VertexAttributeType;

    arti::renderer::VertexBufferLayout layout;
    layout.stride = sizeof(MeshVertex);
    layout.attributes = {
        VertexAttribute{ 0, VertexAttributeType::Float3, offsetof(MeshVertex, position) },
        VertexAttribute{ 1, VertexAttributeType::Float3, offsetof(MeshVertex, normal) },
        VertexAttribute{ 2, VertexAttributeType::Float3, offsetof(MeshVertex, tangent) },
        VertexAttribute{ 3, VertexAttributeType::Float3, offsetof(MeshVertex, bitangent) },
        VertexAttribute{ 4, VertexAttributeType::Float2, offsetof(MeshVertex, uv) },
    };
    return layout;
}

} // namespace

const arti::renderer::VertexBufferLayout& meshVertexLayout()
{
    static const auto layout = makeMeshVertexLayout();
    return layout;
}

} // namespace arti::rendering::detail
