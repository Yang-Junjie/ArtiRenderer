#pragma once
#include "handle.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace arti::rendering {
// 着色模型。延迟管线下只有 metallic-roughness 一种被求值 —— Unlit 和 Blinn-Phong 已经整个
// 移除，前者在延迟管线里没有天然位置，后者的 specular_color / shininess 在
// metallic-roughness 里没有落点。旧格式（MTL 的 Ns 之类）的折算归导入器，不归渲染端。
enum class MaterialType : uint8_t {
    PBR = 0,
    // 引擎侧自定义的着色，渲染端不认识、也不画。留着是为了让「这个材质不走内建管线」
    // 有一个能表达的值，而不是靠一个无效句柄。
    UserType,
};
struct Material {
    MaterialType type{ MaterialType::PBR };

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
