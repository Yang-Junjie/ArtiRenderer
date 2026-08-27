#pragma once
#include "linear_pass.h"
#include "material.h"
#include "render_scene.h"
#include "resource_registry.h"

#include <optional>

namespace arti::rendering::detail {

// 一个 DrawItem 解析成可以直接下 draw call 的东西。
struct ResolvedDraw {
    const GPUMesh* mesh{ nullptr };
    const Submesh* submesh{ nullptr };
    Material material;
};

// 校验 mesh 句柄、submesh 下标和索引数，顺便解析材质（缺失时退回默认材质，不要因为材质丢了就
// 丢掉几何体）。跳过时返回 nullopt，需要提醒的情况会打日志。
//
// 抽出来是因为不透明材质拆成多个 pass 之后，这段校验每个 pass 都要做一遍；重复三次以上就会
// 有人只改其中一处。
std::optional<ResolvedDraw> resolveDraw(const FrameContext& frame, const DrawItem& draw);

} // namespace arti::rendering::detail
