#pragma once
#include "artichoco/renderer/index_buffer.h"
#include "artichoco/renderer/vertex_buffer.h"

#include <nvrhi/nvrhi.h>

#include <vector>

namespace arti::rendering::detail {

nvrhi::Format toNvrhiVertexFormat(arti::renderer::VertexAttributeType type);
nvrhi::Format toNvrhiIndexFormat(arti::renderer::IndexType type);

// 把 RHI 的顶点布局翻成 nvrhi 的 attribute 数组。数组顺序即 Vulkan location。
std::vector<nvrhi::VertexAttributeDesc> toNvrhiAttributes(const arti::renderer::VertexBufferLayout& layout);

} // namespace arti::rendering::detail
