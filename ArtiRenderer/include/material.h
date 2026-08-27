#pragma once
#include "handle.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace arti::rendering {
enum class MaterialType : uint8_t {
    Unlit = 0,
    BlinnPhong,
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

    // Blinn-Phong 参数。只有 type == BlinnPhong 时被消费；Unlit 材质走原来的
    // base_color * base_color_texture，不受这几项影响。
    //
    // shininess 是半程向量点积的指数：值越大高光越小越锐。16 大致是塑料，64~256 是抛光金属。
    glm::vec3 specular_color{ 1.0f, 1.0f, 1.0f };
    float specular_strength{ 0.5f };
    float shininess{ 32.0f };

    TextureHandle base_color_texture;
    TextureHandle metallic_roughness_texture;
    TextureHandle normal_texture;
    TextureHandle occlusion_texture;
    TextureHandle emissive_texture;
};

} // namespace arti::rendering
