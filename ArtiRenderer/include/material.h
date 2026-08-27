#pragma once
#include "handle.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace arti::rendering {
enum class MaterialType : uint8_t {
    Unlit = 0,
    PBR,
    UserType,
};
struct Material {
    MaterialType type{ MaterialType::Unlit };

    glm::vec4 base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic_strength{ 0.0f };
    float roughness_strength{ 1.0f };
    float occlusion_strength{ 1.0f };
    float emissive_strength{ 0.0f };

    TextureHandle base_color_texture;
    TextureHandle metallic_roughness_texture;
    TextureHandle normal_texture;
    TextureHandle occlusion_texture;
    TextureHandle emissive_texture;
};

} // namespace arti::rendering
