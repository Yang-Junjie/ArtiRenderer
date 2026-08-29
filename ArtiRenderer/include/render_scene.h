#pragma once
#include "aabb.h"
#include "handle.h"
#include "light.h"

#include <cstdint>

#include <vector>

namespace arti::rendering {

struct RenderView {
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::vec3 camera_position{ 0.0f };
};

struct DrawItem {
    MeshHandle mesh;
    uint32_t submesh_index{ 0 };
    MaterialHandle material;
    glm::mat4 transform{ 1.0f };
    AABB world_bounds;
    uint32_t picking_id{ 0 };
};

// 拾取请求。坐标是渲染目标内的像素（左上原点），不是窗口坐标 ——
// 编辑器模式下场景画在 Viewport 面板里，两者不是一回事。
struct PickRequest {
    uint32_t x{ 0 };
    uint32_t y{ 0 };
};

struct PickResult {
    // DrawItem::picking_id，0 表示空
    uint32_t picking_id{ 0 };
    uint32_t x{ 0 };
    uint32_t y{ 0 };
};

struct RenderScene {
    RenderView view;
    std::vector<DrawItem> draws;
    std::vector<LightDesc> lights;
    glm::vec4 clear_color{ 0.04f, 0.08f, 0.12f, 1.0f };

    // 线性曝光倍率，在 tone mapping 之前乘上去。1.0 表示「光照算出来的亮度直接进曲线」。
    //
    // 放在场景上而不是相机上（RenderView）是跟 clear_color 一致的取舍：这两个都是「这一帧
    // 怎么呈现」的旋钮，不是场景内容。真做多相机、每个相机自己的曝光时再搬进 RenderView。
    float exposure{ 1.0f };
};

} // namespace arti::rendering
