#include "resource_registry.h"

#include "format_mapping.h"
#include "log.h"
#include "mesh_vertex_layout.h"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace arti::rendering::detail {
namespace {

TextureDesc makeSolidTextureDesc(std::span<const std::byte> texels, std::string debug_name) {
    TextureDesc desc;
    desc.texels = texels;
    desc.width = 1;
    desc.height = 1;
    desc.format = TextureFormat::RGBA8Unorm;
    desc.generate_mipmaps = false;
    desc.debug_name = std::move(debug_name);
    return desc;
}

} // namespace

ResourceRegistry::ResourceRegistry(arti::renderer::RenderDevice& device)
        : m_device(&device) {
    constexpr std::array<uint8_t, 4> white_texels{ 255, 255, 255, 255 };
    constexpr std::array<uint8_t, 4> flat_normal_texels{ 128, 128, 255, 255 };

    m_white_texture = createTexture(makeSolidTextureDesc(std::as_bytes(std::span{ white_texels }),
            "ArtiRenderer built-in white"));
    m_flat_normal_texture = createTexture(makeSolidTextureDesc(
            std::as_bytes(std::span{ flat_normal_texels }), "ArtiRenderer built-in flat normal"));
}

MeshHandle ResourceRegistry::createMesh(const Mesh& mesh, std::string_view debug_name) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        throw std::invalid_argument("A mesh requires both vertices and indices.");
    }

    const std::string name{ debug_name.empty() ? std::string_view{ "ArtiRenderer Mesh" }
                                               : debug_name };
    const std::string vertex_name = name + " vertices";
    const std::string index_name = name + " indices";

    auto vertex_buffer = m_device->createVertexBuffer(std::as_bytes(std::span{ mesh.vertices }),
            static_cast<uint32_t>(mesh.vertices.size()), meshVertexLayout(), vertex_name);
    auto index_buffer = m_device->createIndexBuffer(std::as_bytes(std::span{ mesh.indices }),
            static_cast<uint32_t>(mesh.indices.size()), arti::renderer::IndexType::UInt32,
            index_name);

    GPUMesh gpu_mesh{ std::move(vertex_buffer), std::move(index_buffer), mesh.submeshes,
        mesh.bounds };

    // 没给 submesh 就当整个网格是一个 submesh。
    if (gpu_mesh.submeshes.empty()) {
        Submesh submesh;
        submesh.index_count = static_cast<uint32_t>(mesh.indices.size());
        submesh.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
        gpu_mesh.submeshes.push_back(submesh);
    }

    // 没给包围盒就自己算，剔除要用。
    if (gpu_mesh.bounds.isEmpty()) {
        for (const auto& vertex: mesh.vertices) {
            gpu_mesh.bounds.expand(vertex.position);
        }
    }

    const auto submesh_count = gpu_mesh.submeshes.size();
    const auto handle = MeshHandle::generate();
    m_meshes.emplace(handle, std::move(gpu_mesh));
    getLogChannel().debug("Uploaded mesh '{}' ({} vertices, {} indices, {} submeshes)", 
            name,mesh.vertices.size(), mesh.indices.size(), submesh_count);
    return handle;
}

TextureHandle ResourceRegistry::createTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        throw std::invalid_argument("A texture requires a non-zero size.");
    }

    auto texture = m_device->createTexture2D(desc.texels, desc.width, desc.height,
            toRHIFormat(desc.format), desc.generate_mipmaps, desc.debug_name);

    const auto handle = TextureHandle::generate();
    m_textures.emplace(handle, std::move(texture));
    return handle;
}

MaterialHandle ResourceRegistry::createMaterial(const Material& material) {
    const auto handle = MaterialHandle::generate();
    m_materials.emplace(handle, material);
    return handle;
}

bool ResourceRegistry::updateMaterial(MaterialHandle handle, const Material& material) {
    const auto entry = m_materials.find(handle);
    if (entry == m_materials.end()) {
        return false;
    }
    entry->second = material;
    return true;
}

bool ResourceRegistry::destroyMesh(MeshHandle handle) { return m_meshes.erase(handle) != 0; }

bool ResourceRegistry::destroyTexture(TextureHandle handle) {
    if (handle == m_white_texture || handle == m_flat_normal_texture) {
        getLogChannel().warn("Refusing to destroy a built-in texture");
        return false;
    }
    return m_textures.erase(handle) != 0;
}

bool ResourceRegistry::destroyMaterial(MaterialHandle handle) {
    return m_materials.erase(handle) != 0;
}

const GPUMesh* ResourceRegistry::findMesh(MeshHandle handle) const noexcept {
    const auto entry = m_meshes.find(handle);
    return entry == m_meshes.end() ? nullptr : &entry->second;
}

const arti::renderer::Texture2D* ResourceRegistry::findTexture(
        TextureHandle handle) const noexcept {
    const auto entry = m_textures.find(handle);
    return entry == m_textures.end() ? nullptr : &entry->second;
}

const Material* ResourceRegistry::findMaterial(MaterialHandle handle) const noexcept {
    const auto entry = m_materials.find(handle);
    return entry == m_materials.end() ? nullptr : &entry->second;
}

const arti::renderer::Texture2D& ResourceRegistry::resolveTexture(TextureHandle handle) const {
    if (const auto* texture = findTexture(handle)) {
        return *texture;
    }
    const auto* fallback = findTexture(m_white_texture);
    if (fallback == nullptr) {
        throw std::logic_error("The built-in white texture is missing.");
    }
    return *fallback;
}

} // namespace arti::rendering::detail
