#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace arti::rendering {

enum class TextureFormat {
    RGBA8Unorm,
    RGBA8Srgb,
    RGBA16Float,
};

struct TextureDesc {
    std::span<const std::byte> texels;
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    TextureFormat format{ TextureFormat::RGBA8Srgb };
    bool generate_mipmaps {true};
    std::string debug_name{ "Texture" };
};

struct TextureInfo {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint32_t mip_levels{ 0 };
    TextureFormat format{ TextureFormat::RGBA8Srgb };
    bool built_in{ false };
};

} // namespace arti::rendering
