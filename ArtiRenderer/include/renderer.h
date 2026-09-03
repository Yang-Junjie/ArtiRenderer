#pragma once

#include "debug_draw.h"
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
    // 延迟渲染。几何写 G-Buffer，光照在一个全屏 pass 里一次算完。
    // 目前只有这一条 —— 前向管线已经整条移除，不留双路径。
    Deferred = 0,
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
    PipelineKind pipeline{ PipelineKind::Deferred };
    PresentMode present{ PresentMode::Direct };
};

struct FrameStatistics {
    uint32_t draw_calls{ 0 };
    uint32_t submeshes{ 0 };
    uint32_t culled{ 0 };
    // 四级 cascade 上被跳过的 draw 次数之和。一张图四级各画一次所以最大是 4 × submeshes。
    // 和 culled 分开：culled 是相机视锥的账，shadow_culled 是光空间 XY 的账，两个数加起来
    // 没有守恒式。
    uint32_t shadow_culled{ 0 };
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

    // GPU 拾取。坐标是场景渲染目标内的像素（左上原点），编辑器模式下就是 Viewport 面板内的位置。
    //
    // 必须在 renderFrame() **之前**调：请求要赶上这一帧的 ID 缓冲绘制。
    // 只作用于紧接着的那一帧，不会残留。
    void requestPick(const PickRequest& request) noexcept;

    // 取走已经读回来的拾取结果（取走即清空）。读回是异步的，所以从 requestPick 到这里
    // 返回非空之间会隔几帧 —— 调用方每帧问一次即可，不要阻塞等它。
    //
    // picking_id 是 DrawItem 里填的那个值；0 表示点在空处。
    std::optional<PickResult> takePickResult() noexcept;

    // 调试绘制。和 requestPick() 一样是「攒到下一帧」的形状：这些线只画紧接着的那一帧，
    // renderFrame() 消费完就清空，调用方不用管生命周期，也不用把一个列表层层传下来。
    //
    // 线画在 tone mapping 之后的显示层上，所以 color 写什么就看到什么（见 DebugLine）。
    // 深度测试是开的：被场景里的物体挡住的线看不见 —— 线在世界里是有位置的。
    void drawLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);

    // 线框盒。12 条边，bounds 是世界空间的 —— 局部包围盒用 AABB::transformed() 转过来。
    void drawAABB(const AABB& bounds, const glm::vec4& color);

    // 三个正交大圆拼出来的线框球。点光源的 range 就靠它看。
    // segments 是每个圆的分段数，24 段在常见距离上已经看不出棱角。
    void drawWireSphere(const glm::vec3& center, float radius, const glm::vec4& color,
            uint32_t segments = 24);

    RenderOutputInfo outputInfo() const noexcept;
    void waitIdle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::rendering
