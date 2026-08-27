#include "draw_resolve.h"

#include "log.h"

namespace arti::rendering::detail {

std::optional<ResolvedDraw> resolveDraw(const FrameContext& frame, const DrawItem& draw)
{
    const auto* mesh = frame.resources().findMesh(draw.mesh);
    if (mesh == nullptr) {
        getLogChannel().warn("Skipping draw with unknown mesh {}", draw.mesh.toString());
        return std::nullopt;
    }
    if (draw.submesh_index >= mesh->submeshes.size()) {
        getLogChannel().warn("Skipping draw with out-of-range submesh {} on mesh {}",
                draw.submesh_index, draw.mesh.toString());
        return std::nullopt;
    }

    const auto& submesh = mesh->submeshes[draw.submesh_index];
    // 空 submesh 是合法的，不用报警。
    if (submesh.index_count == 0) {
        return std::nullopt;
    }

    ResolvedDraw resolved;
    resolved.mesh = mesh;
    resolved.submesh = &submesh;
    // 材质缺失时用默认材质（白色 unlit）。
    if (const auto* material = frame.resources().findMaterial(draw.material)) {
        resolved.material = *material;
    }
    return resolved;
}

} // namespace arti::rendering::detail
