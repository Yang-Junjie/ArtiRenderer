#pragma once

#include "handle.h"
#include "material.h"
#include "mesh.h"
#include "render_output.h"
#include "render_scene.h"
#include "texture.h"

#include <cstdint>

#include <memory>
#include <optional>
#include <string_view>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {


enum class PipelineKind : uint8_t {
    Forward = 0,
};

struct RendererCreateInfo {
    PipelineKind pipeline{ PipelineKind::Forward };
};

struct FrameStatistics {
    uint32_t draw_calls{ 0 };
    uint32_t submeshes{ 0 };
    uint32_t culled{ 0 };
    bool rendered{ false };
};


// 生命周期上不拥有 RenderDevice 谁建窗口和 surface 谁建 device
class Renderer {
public:
    Renderer(arti::renderer::RenderDevice& device, const RendererCreateInfo& info = {});
    ~Renderer();

    MeshHandle createMesh(const Mesh& mesh, std::string_view debug_name = {});
    TextureHandle createTexture(const TextureDesc& desc);
    MaterialHandle createMaterial(const Material& material);

    bool updateMaterial(MaterialHandle handle, const Material& material);

    bool destroyMesh(MeshHandle handle);
    bool destroyTexture(TextureHandle handle);
    bool destroyMaterial(MaterialHandle handle);

    std::optional<TextureInfo> textureInfo(TextureHandle handle) const;
    std::optional<Material> material(MaterialHandle handle) const;

    TextureHandle whiteTexture() const noexcept;
    TextureHandle flatNormalTexture() const noexcept;

    FrameStatistics renderFrame(const RenderScene& scene);

    RenderOutputInfo outputInfo() const noexcept;
    void waitIdle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
