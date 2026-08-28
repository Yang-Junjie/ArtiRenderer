#pragma once

#include "frame_overlay.h"
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

// 场景最终怎么到屏幕上。
enum class PresentMode : uint8_t {
    // 运行时。PresentPass 把 SceneColor 贴到 backbuffer，UI（如果有）盖在上面。
    Direct = 0,
    // 编辑器。PresentPass 关掉，场景留在离屏的 SceneColor 里，由宿主用
    // ImGui::Image(sceneColorTextureId(), ...) 放进某个面板；这一帧的 backbuffer 完全由 UI 拥有。
    IntoUI,
};

struct RendererCreateInfo {
    PipelineKind pipeline{ PipelineKind::Forward };
    PresentMode present{ PresentMode::Direct };
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
    // 上传之后查网格的包围盒和规模。视锥剔除要靠它拿到局部包围盒 —— 顶点数据已经不在 CPU 侧了。
    std::optional<MeshInfo> meshInfo(MeshHandle handle) const;

    TextureHandle whiteTexture() const noexcept;
    TextureHandle flatNormalTexture() const noexcept;

    // overlay 默认为空 —— 不用 UI 的调用方一个字都不用改。
    FrameStatistics renderFrame(const RenderScene& scene, const FrameOverlay& overlay = {});

    // 运行时可切：编辑器里在「全屏预览」和「场景进面板」之间来回是常见操作，
    // 不值得为它重建一条管线。
    void setPresentMode(PresentMode mode) noexcept;
    PresentMode presentMode() const noexcept;

    // 场景渲染到多大。宽或高为 0 表示跟着输出走（Direct 模式的常规做法）。
    // IntoUI 模式下宿主应该按面板的像素尺寸设置它，否则场景会按窗口尺寸渲染再被缩放。
    void setSceneTargetSize(uint32_t width, uint32_t height) noexcept;

    // 把 SceneColor 喂给 ImGui::Image() 用的 id。只在 IntoUI 模式下有内容可采
    // —— Direct 模式下 SceneColor 每帧确实也画了，但那一帧的它已经被贴进 backbuffer 了。
    uint64_t sceneColorTextureId() const noexcept;

    RenderOutputInfo outputInfo() const noexcept;
    void waitIdle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
