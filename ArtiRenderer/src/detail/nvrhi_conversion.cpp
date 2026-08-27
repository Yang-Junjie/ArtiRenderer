#include "nvrhi_conversion.h"

#include <stdexcept>

namespace arti::rendering::detail {
namespace {

// 语义名只对 D3D 后端有意义，Vulkan 后端完全忽略它。
// 留着是为了调试输出和以后可能的 D3D 后端。
const char* semanticName(uint32_t location)
{
    switch (location) {
        case 0:
            return "POSITION";
        case 1:
            return "NORMAL";
        case 2:
            return "TANGENT";
        case 3:
            return "BINORMAL";
        case 4:
            return "TEXCOORD0";
        default:
            return "ATTRIBUTE";
    }
}

} // namespace

nvrhi::Format toNvrhiVertexFormat(arti::renderer::VertexAttributeType type)
{
    switch (type) {
        case arti::renderer::VertexAttributeType::Float2:
            return nvrhi::Format::RG32_FLOAT;
        case arti::renderer::VertexAttributeType::Float3:
            return nvrhi::Format::RGB32_FLOAT;
        case arti::renderer::VertexAttributeType::Float4:
            return nvrhi::Format::RGBA32_FLOAT;
    }
    throw std::invalid_argument("Unsupported NVRHI vertex attribute type.");
}

nvrhi::Format toNvrhiIndexFormat(arti::renderer::IndexType type)
{
    switch (type) {
        case arti::renderer::IndexType::UInt16:
            return nvrhi::Format::R16_UINT;
        case arti::renderer::IndexType::UInt32:
            return nvrhi::Format::R32_UINT;
    }
    throw std::invalid_argument("Unsupported NVRHI index type.");
}

std::vector<nvrhi::VertexAttributeDesc> toNvrhiAttributes(
        const arti::renderer::VertexBufferLayout& layout)
{
    std::vector<nvrhi::VertexAttributeDesc> attributes;
    attributes.reserve(layout.attributes.size());
    for (const auto& attribute: layout.attributes) {
        nvrhi::VertexAttributeDesc desc;
        desc.setName(semanticName(attribute.location))
                .setFormat(toNvrhiVertexFormat(attribute.type))
                .setBufferIndex(0)
                .setOffset(attribute.offset)
                .setElementStride(layout.stride);
        attributes.push_back(desc);
    }
    return attributes;
}

} // namespace arti::rendering::detail
