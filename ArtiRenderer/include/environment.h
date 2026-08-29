#pragma once
#include "handle.h"

#include <glm/vec4.hpp>

namespace arti::rendering {

// 场景的环境光照。
//
// 现在只有 sky_color / intensity / enabled 会被消费：opaque pass 把它们当成一个常数环境光，
// 也就是原来硬编码在 pass 里的那个 0.03。equirectangular_texture 是给 IBL 留的入口。
struct EnvironmentDesc {
    // 线性 HDR 的等距柱状投影（equirectangular）源图，IBL 的烘焙输入 ——
    // irradiance cube、prefiltered cube 都从它来。空句柄表示没有环境贴图，此时只剩 sky_color。
    //
    // **目前还没有被消费**：渲染端还没有 cubemap 通路，也没有 equirect -> cube 的烘焙。
    // 提前放进来是因为 EnvironmentComponent 的全部意义就是引用这张图 ——
    // 没有它，那个组件只是个颜色选择器。
    TextureHandle equirectangular_texture;

    // 没有环境贴图时的常数环境光；有贴图之后它是 irradiance 的兜底。
    // 默认值刻意不是纯灰：天光偏蓝在现实里是常态，纯灰会让所有材质看上去发闷。
    glm::vec4 sky_color{ 0.03f, 0.03f, 0.035f, 1.0f };

    // 环境光的线性强度倍率。**不是光度学单位** —— 和 LightDesc::intensity 一样只是个纯倍数，
    // 1.0 表示 sky_color（将来是环境贴图）的值直接进着色。
    //
    // 旧版这里是 lux、默认 30000，靠相机曝光折算回场景线性。现在曝光归 TonemapPass，
    // 着色端只认场景线性辐射度，所以这条不跟。
    float intensity{ 1.0f };

    // 关掉就完全没有环境项，只剩直接光。默认开启，配合上面的默认值正好等于
    // 引入这个结构之前 pass 里硬编码的那个环境光，所以老场景的观感不变。
    bool enabled{ true };

    // 要不要把环境画成天空背景（SkyPass）。关掉时背景是 clear_color，但 IBL 照常起作用 ——
    // 想让物体接受环境光而背景保持纯色时用得上（比如产品渲染）。
    // 没有环境贴图时这一项没有意义：天空采的就是烘出来的那张 cube。
    bool sky_visible{ true };
};

} // namespace arti::rendering
