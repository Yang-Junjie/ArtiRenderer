#include "basic_window_layer.h"

#include "artichoco/core/application.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/renderer/render_device.h"
#include "imgui_host.h"

#include <array>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <span>
#include <utility>
#include <vector>

namespace arti::sample {
namespace {

// 立方体：每个面 4 个顶点，方便给独立的法线和 UV。
// 三角形按「从外面看逆时针」编写，与 GBufferPass 的正面约定一致。
rendering::Mesh makeCubeMesh()
{
    struct FaceDesc {
        glm::vec3 normal;
        glm::vec3 origin;
        glm::vec3 right;
        glm::vec3 up;
    };

    constexpr std::array<FaceDesc, 6> faces{ {
            // +X
            { { 1, 0, 0 }, { 1, -1, 1 }, { 0, 0, -2 }, { 0, 2, 0 } },
            // -X
            { { -1, 0, 0 }, { -1, -1, -1 }, { 0, 0, 2 }, { 0, 2, 0 } },
            // +Y
            { { 0, 1, 0 }, { -1, 1, 1 }, { 2, 0, 0 }, { 0, 0, -2 } },
            // -Y
            { { 0, -1, 0 }, { -1, -1, -1 }, { 2, 0, 0 }, { 0, 0, 2 } },
            // +Z
            { { 0, 0, 1 }, { -1, -1, 1 }, { 2, 0, 0 }, { 0, 2, 0 } },
            // -Z
            { { 0, 0, -1 }, { 1, -1, -1 }, { -2, 0, 0 }, { 0, 2, 0 } },
    } };

    rendering::Mesh mesh;
    mesh.vertices.reserve(faces.size() * 4);
    mesh.indices.reserve(faces.size() * 6);

    for (const auto& face: faces) {
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const std::array<glm::vec2, 4> uvs{ { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f },
                { 0.0f, 0.0f } } };
        const std::array<glm::vec3, 4> corners{ {
                face.origin,
                face.origin + face.right,
                face.origin + face.right + face.up,
                face.origin + face.up,
        } };

        for (size_t corner = 0; corner < corners.size(); ++corner) {
            rendering::MeshVertex vertex;
            vertex.position = corners[corner] * 0.5f;
            vertex.normal = face.normal;
            vertex.tangent = glm::normalize(face.right);
            vertex.bitangent = glm::normalize(face.up);
            vertex.uv = uvs[corner];
            mesh.vertices.push_back(vertex);
            mesh.bounds.expand(vertex.position);
        }

        for (const uint32_t offset: { 0U, 1U, 2U, 0U, 2U, 3U }) {
            mesh.indices.push_back(base + offset);
        }
    }

    return mesh;
}

// 8x8 的黑白棋盘，看得出 UV 和透视是否正确。
std::vector<std::byte> makeCheckerTexels(uint32_t size)
{
    std::vector<std::byte> texels(static_cast<size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const auto value = static_cast<std::byte>(light ? 230 : 60);
            const size_t offset = (static_cast<size_t>(y) * size + x) * 4;
            texels[offset + 0] = value;
            texels[offset + 1] = value;
            texels[offset + 2] = value;
            texels[offset + 3] = static_cast<std::byte>(255);
        }
    }
    return texels;
}

} // namespace

BasicWindowLayer::BasicWindowLayer(bool enable_renderer, uint32_t frame_limit,
        bool show_imgui_demo, bool editor_mode, bool vsync)
    : Layer("BasicWindowLayer")
    , m_enable_renderer(enable_renderer)
    , m_frame_limit(frame_limit)
    , m_show_demo_window(show_imgui_demo)
    , m_editor_mode(editor_mode)
    , m_vsync(vsync)
{}

BasicWindowLayer::~BasicWindowLayer() = default;

void BasicWindowLayer::onAttach()
{
    auto& app = core::Application::get();

    if (!m_enable_renderer) {
        app.getLogChannel().info("Renderer disabled, running window loop only");
        return;
    }

    auto surface_source = platform::createSDLVulkanSurfaceSource(app.getWindow());
    renderer::RenderDeviceCreateInfo device_info;
    device_info.application_name = "ArtiRenderer";
    device_info.vsync = m_vsync;
    m_render_device = std::make_unique<renderer::RenderDevice>(
            app.getWindow(), std::move(surface_source), device_info);

    rendering::RendererCreateInfo renderer_info;
    renderer_info.present =
            m_editor_mode ? rendering::PresentMode::IntoUI : rendering::PresentMode::Direct;
    m_renderer = std::make_unique<rendering::Renderer>(*m_render_device, renderer_info);
    createSceneResources();

    ImGuiHostCreateInfo imgui_info;
    // 帧数受限说明这是自动化跑的，不要让它继承或写下 imgui.ini —— 布局得是可复现的。
    imgui_info.persist_layout = m_frame_limit == 0;
    // renderer 先建：字体图集是通过它上传的。
    m_imgui = std::make_unique<ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);

    const auto output = m_renderer->outputInfo();
    app.getLogChannel().info("Renderer ready, output {}x{} (available: {})", output.width,
            output.height, output.available);
}

void BasicWindowLayer::createSceneResources()
{
    constexpr uint32_t checker_size = 64;
    const auto texels = makeCheckerTexels(checker_size);

    rendering::TextureDesc texture_desc;
    texture_desc.texels = std::span{ texels };
    texture_desc.width = checker_size;
    texture_desc.height = checker_size;
    texture_desc.format = rendering::TextureFormat::RGBA8Unorm;
    texture_desc.debug_name = "Sample checker";
    m_checker_texture = m_renderer->createTexture(texture_desc);

    rendering::Material material;
    // 管线只有 metallic-roughness 一种着色模型，所以不用设 type（默认就是 PBR）。
    material.base_color = glm::vec4{ 1.0f, 0.85f, 0.7f, 1.0f };
    material.base_color_texture = m_checker_texture;
    // 介质 + 半光泽：0.5 的 roughness 高光够宽，不会把棋盘的亮格冲掉。
    material.metallic_strength = 0.0f;
    material.roughness_strength = 0.5f;
    m_cube_material = m_renderer->createMaterial(material);

    m_cube_mesh = m_renderer->createMesh(makeCubeMesh(), "Sample cube");
}

void BasicWindowLayer::onDetach()
{
    // waitIdle 必须在销毁 ImGuiHost 之前：它的析构会销毁字体图集纹理，而 GPU 可能还在用。
    if (m_renderer) {
        m_renderer->waitIdle();
    }
    m_imgui.reset();
    m_renderer.reset();
    m_render_device.reset();
}

void BasicWindowLayer::onUpdate(core::Timestep delta_time)
{
    if (m_rotate) {
        m_elapsed_seconds += delta_time.getSeconds();
    }
    ++m_frame_index;

    if (m_frame_limit != 0 && m_frame_index >= m_frame_limit) {
        core::Application::get().getLogChannel().info(
                "Frame limit reached after {} frames", m_frame_index);
        core::Application::get().close();
    }
}

void BasicWindowLayer::onImGuiRender()
{
    if (!m_imgui) {
        return;
    }

    // Application 的循环是 onUpdate -> onImGuiRender -> onRender，所以这里画完的 draw data
    // 正好赶上同一帧的 renderFrame。
    m_imgui->beginFrame();
    // 停靠区要在其它窗口之前建，否则那些窗口这一帧停靠不上去。
    m_imgui->dockSpaceOverViewport();
    drawUI();
    if (m_editor_mode) {
        drawViewportPanel();
    }
    m_imgui->endFrame();
}

void BasicWindowLayer::drawViewportPanel()
{
    // 必须给初始尺寸：Image 的大小取自 GetContentRegionAvail()，而窗口默认会自适应内容 ——
    // 两者互相取值，结果会塌缩到最小尺寸（实测 32x13，场景就渲染成那么大）。
    // 停靠之后尺寸由 dock node 决定，这个循环依赖自然消失。
    ImGui::SetNextWindowSize(ImVec2{ 960.0f, 540.0f }, ImGuiCond_FirstUseEver);
    // 面板内没有留白，这样 Image 正好铺满，量出来的尺寸就是场景该渲染的尺寸。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    // 面板被折叠或拖成负值时先记 0，renderFrame 那边会回退到窗口尺寸。
    m_scene_width = available.x > 0.0f ? static_cast<uint32_t>(available.x) : 0;
    m_scene_height = available.y > 0.0f ? static_cast<uint32_t>(available.y) : 0;

    if (m_scene_width != 0 && m_scene_height != 0) {
        // SceneColor 是线性数据，ImGuiPass 认出这个 id 后会跳过 sRGB 解码。
        ImGui::Image(m_renderer->sceneColorTextureId(),
                ImVec2{ static_cast<float>(m_scene_width), static_cast<float>(m_scene_height) });
    }

    ImGui::End();
}

void BasicWindowLayer::drawUI()
{
    ImGui::SetNextWindowSize(ImVec2{ 320.0f, 0.0f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("ArtiRenderer");

    ImGui::Text("Dear ImGui %s", ImGui::GetVersion());
    ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate,
            1000.0f / ImGui::GetIO().Framerate);

    ImGui::SeparatorText("Frame");
    ImGui::Text("draw calls: %u", m_last_statistics.draw_calls);
    ImGui::Text("submeshes:  %u", m_last_statistics.submeshes);
    const auto output = m_renderer->outputInfo();
    ImGui::Text("output:     %ux%u", output.width, output.height);

    ImGui::SeparatorText("Present");
    if (m_render_device) {
        bool vsync = m_render_device->vsync();
        if (ImGui::Checkbox("VSync", &vsync)) {
            m_render_device->setVsync(vsync);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(renderer::toString(m_render_device->swapchainInfo().present_mode));
    }
    // Direct：场景贴到 backbuffer，UI 盖在上面（停靠区的中央节点是透传的，所以场景照样看得见）。
    // Editor：场景留在 SceneColor，由下面那个 Viewport 面板显示。
    if (ImGui::Checkbox("Editor mode (scene into panel)", &m_editor_mode)) {
        m_renderer->setPresentMode(m_editor_mode ? rendering::PresentMode::IntoUI
                                                 : rendering::PresentMode::Direct);
        // 切回 Direct 时把请求尺寸清掉，否则场景会一直按上次面板的大小渲染。
        if (!m_editor_mode) {
            m_scene_width = 0;
            m_scene_height = 0;
        }
    }
    if (m_editor_mode) {
        ImGui::Text("scene target: %ux%u", m_scene_width, m_scene_height);
    }

    ImGui::SeparatorText("Scene");
    ImGui::Checkbox("Rotate", &m_rotate);
    ImGui::ColorEdit3("Clear color", &m_clear_color.x);

    ImGui::SeparatorText("Debug");
    ImGui::Checkbox("ImGui demo window", &m_show_demo_window);

    ImGui::End();

    // demo 窗口把 scissor、大网格的 vtx offset、多 draw list 都走一遍，是 pass 的实用冒烟测试。
    if (m_show_demo_window) {
        ImGui::ShowDemoWindow(&m_show_demo_window);
    }
}

void BasicWindowLayer::onRender()
{
    if (!m_renderer) {
        return;
    }

    const auto output = m_renderer->outputInfo();
    if (!output.available || output.height == 0) {
        return;
    }

    // 编辑器模式下场景按 Viewport 面板的尺寸渲染；0 表示跟着窗口走。
    m_renderer->setSceneTargetSize(m_scene_width, m_scene_height);

    // 投影的宽高比要用场景实际渲染的尺寸，不是窗口的 —— 否则场景进面板之后会被拉变形。
    const bool has_scene_size = m_scene_width != 0 && m_scene_height != 0;
    const float aspect = has_scene_size
            ? static_cast<float>(m_scene_width) / static_cast<float>(m_scene_height)
            : static_cast<float>(output.width) / static_cast<float>(output.height);

    rendering::RenderScene scene;
    scene.clear_color = glm::vec4{ m_clear_color, 1.0f };
    scene.view.camera_position = glm::vec3{ 0.0f, 1.2f, 3.0f };
    scene.view.view = glm::lookAt(
            scene.view.camera_position, glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f });
    // 显式用 RH_ZO：深度范围是 [0,1]。
    //
    // 不需要翻 Y。NVRHI 对外是 D3D 的 NDC 约定（+Y 朝上），Vulkan 后端用负高度 viewport
    // 在内部完成转换 —— 见 nvrhi/src/vulkan/vulkan-graphics.cpp 的 VKViewportWithDXCoords()。
    // 所以这里按 D3D 约定给矩阵即可，自己再翻一次会让画面上下颠倒。
    scene.view.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);

    // 方向光。direction 是光的传播方向（从光源射出），不是从表面指向光源 ——
    // DeferredLightingPass 会取反。这里让光从右上前方斜射下来，立方体转动时能看出高光在游走。
    rendering::LightDesc sun;
    sun.type = rendering::LightType::Directional;
    sun.direction = glm::normalize(glm::vec3{ -0.5f, -1.0f, -0.35f });
    sun.color = glm::vec4{ 1.0f, 0.96f, 0.9f, 1.0f };
    sun.intensity = 1.0f;
    scene.lights.push_back(sun);

    rendering::DrawItem draw;
    draw.mesh = m_cube_mesh;
    draw.material = m_cube_material;
    draw.transform = glm::rotate(glm::mat4{ 1.0f }, m_elapsed_seconds * 0.8f,
            glm::normalize(glm::vec3{ 0.3f, 1.0f, 0.2f }));
    scene.draws.push_back(draw);

    // overlay 为空（比如没建 ImGuiHost）时 ImGuiPass 整体跳过，这里不用分支。
    const auto statistics = m_renderer->renderFrame(
            scene, m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});
    m_last_statistics = statistics;
    if (m_frame_index == 1 && statistics.rendered) {
        core::Application::get().getLogChannel().info(
                "First frame rendered ({} draw calls)", statistics.draw_calls);
    }
}

} // namespace arti::sample
