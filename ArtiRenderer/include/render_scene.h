#pragma once
#include "aabb.h"
#include "environment.h"
#include "handle.h"
#include "light.h"

#include <cstdint>

#include <vector>

namespace arti::rendering {

struct RenderView {
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::vec3 camera_position{ 0.0f };

    // 相机的近远平面。级联阴影要按距离切视锥，所以必须拿到这两个标量。
    //
    // 单独存而不是从 projection 反解：反解一个透视矩阵的 near / far 是能做的，但公式对
    // 「正交还是透视」「ZO 还是 NO 深度约定」都敏感，而生产者手上本来就有原值 —— 让每个
    // 消费者去反解，等于把一个脆弱的推导复制好几份。
    //
    // 填它的地方有两处（场景相机的抽取、编辑器相机），**漏一处的表现是「编辑器里阴影对、
    // Play 模式里不对」**，而两边单独看都像是对的。
    float near_plane{ 0.1f };
    float far_plane{ 100.0f };
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
    // 环境光照。一个场景一份 —— 不像灯光那样是个列表，因为「环境」就是唯一的那个背景。
    EnvironmentDesc environment;
    glm::vec4 clear_color{ 0.04f, 0.08f, 0.12f, 1.0f };

    // 线性曝光倍率，在 tone mapping 之前乘上去。1.0 表示「光照算出来的亮度直接进曲线」。
    //
    // 放在场景上而不是相机上（RenderView）是跟 clear_color 一致的取舍：这两个都是「这一帧
    // 怎么呈现」的旋钮，不是场景内容。真做多相机、每个相机自己的曝光时再搬进 RenderView。
    float exposure{ 1.0f };
};

} // namespace arti::rendering
