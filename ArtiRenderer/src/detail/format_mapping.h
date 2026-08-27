#pragma once
#include "artichoco/renderer/texture_format.h"
#include "texture.h"

#include <stdexcept>

namespace arti::rendering::detail {
inline arti::renderer::TextureFormat toRHIFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA8Unorm:
            return arti::renderer::TextureFormat::RGBA8Unorm;
        case TextureFormat::RGBA8Srgb:
            return arti::renderer::TextureFormat::RGBA8Srgb;
        case TextureFormat::RGBA16Float:
            return arti::renderer::TextureFormat::RGBA16Float;
    }

    throw std::invalid_argument("Unsupported rendering texture format.");
}

inline TextureFormat fromRHIFormat(arti::renderer::TextureFormat format) {
    switch (format) {
        case arti::renderer::TextureFormat::RGBA8Unorm:
            return TextureFormat::RGBA8Unorm;
        case arti::renderer::TextureFormat::RGBA8Srgb:
            return TextureFormat::RGBA8Srgb;
        case arti::renderer::TextureFormat::RGBA16Float:
            return TextureFormat::RGBA16Float;
    }
    throw std::invalid_argument("Unsupported RHI texture format.");
}

} // namespace arti::rendering::detail
