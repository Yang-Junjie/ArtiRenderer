#pragma once
#include "artichoco/renderer/render_device.h"
#include "handle.h"
#include "material.h"
#include "mesh.h"
#include "texture.h"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace arti::rendering::detail {

// 一个 Mesh 上传到 GPU 之后的样子。CPU 侧的顶点数据不保留。
struct GPUMesh {
    arti::renderer::VertexBuffer vertex_buffer;
    arti::renderer::IndexBuffer index_buffer;
    std::vector<Submesh> submeshes;
    AABB bounds;
};

// 句柄 -> GPU 资源的中央注册表。Renderer 独占持有，pass 只读。
class ResourceRegistry {
public:
    explicit ResourceRegistry(arti::renderer::RenderDevice& device);

    ResourceRegistry(const ResourceRegistry&) = delete;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;

    MeshHandle createMesh(const Mesh& mesh, std::string_view debug_name);
    TextureHandle createTexture(const TextureDesc& desc);
    MaterialHandle createMaterial(const Material& material);

    bool updateMaterial(MaterialHandle handle, const Material& material);

    bool destroyMesh(MeshHandle handle);
    bool destroyTexture(TextureHandle handle);
    bool destroyMaterial(MaterialHandle handle);

    const GPUMesh* findMesh(MeshHandle handle) const noexcept;
    const arti::renderer::Texture2D* findTexture(TextureHandle handle) const noexcept;
    const Material* findMaterial(MaterialHandle handle) const noexcept;

    // 空句柄或找不到时回退到内建白图，这样 pass 里不用到处判空。
    const arti::renderer::Texture2D& resolveTexture(TextureHandle handle) const;

    TextureHandle whiteTexture() const noexcept { return m_white_texture; }
    TextureHandle flatNormalTexture() const noexcept { return m_flat_normal_texture; }

private:
    arti::renderer::RenderDevice* m_device{ nullptr };
    std::unordered_map<MeshHandle, GPUMesh> m_meshes;
    std::unordered_map<TextureHandle, arti::renderer::Texture2D> m_textures;
    std::unordered_map<MaterialHandle, Material> m_materials;
    TextureHandle m_white_texture;
    TextureHandle m_flat_normal_texture;
};

} // namespace arti::rendering::detail
