#pragma once
#include "artichoco/renderer/vertex_buffer.h"

namespace arti::rendering::detail {

// [[vk::location(N)]] 要对上这里的顺序。
//   0 position (vec3)  1 normal (vec3)  2 tangent (vec3)
//   3 bitangent (vec3) 4 uv (vec2)
 const arti::renderer::VertexBufferLayout& meshVertexLayout();

} // namespace arti::rendering::detail
