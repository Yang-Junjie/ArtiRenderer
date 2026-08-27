#include "basic_window_layer.h"

#include "artichoco/core/application.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/renderer/render_device.h"

#include <array>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <span>
#include <utility>
#include <vector>

namespace arti::sample {
namespace {

// 立方体：每个面 4 个顶点，方便给独立的法线和 UV。
// 三角形按「从外面看逆时针」编写，与 ForwardOpaquePass 的正面约定一致。
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

BasicWindowLayer::BasicWindowLayer(bool enable_renderer, uint32_t frame_limit)
    : Layer("BasicWindowLayer")
    , m_enable_renderer(enable_renderer)
    , m_frame_limit(frame_limit)
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
    m_render_device = std::make_unique<renderer::RenderDevice>(
            app.getWindow(), std::move(surface_source), device_info);

    m_renderer = std::make_unique<rendering::Renderer>(*m_render_device);
    createSceneResources();

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
    material.type = rendering::MaterialType::Unlit;
    material.base_color = glm::vec4{ 1.0f, 0.85f, 0.7f, 1.0f };
    material.base_color_texture = m_checker_texture;
    m_cube_material = m_renderer->createMaterial(material);

    m_cube_mesh = m_renderer->createMesh(makeCubeMesh(), "Sample cube");
}

void BasicWindowLayer::onDetach()
{
    if (m_renderer) {
        m_renderer->waitIdle();
    }
    m_renderer.reset();
    m_render_device.reset();
}

void BasicWindowLayer::onUpdate(core::Timestep delta_time)
{
    m_elapsed_seconds += delta_time.getSeconds();
    ++m_frame_index;

    if (m_frame_limit != 0 && m_frame_index >= m_frame_limit) {
        core::Application::get().getLogChannel().info(
                "Frame limit reached after {} frames", m_frame_index);
        core::Application::get().close();
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
    const float aspect = static_cast<float>(output.width) / static_cast<float>(output.height);

    rendering::RenderScene scene;
    scene.clear_color = glm::vec4{ 0.05f, 0.07f, 0.10f, 1.0f };
    scene.view.camera_position = glm::vec3{ 0.0f, 1.2f, 3.0f };
    scene.view.view = glm::lookAt(
            scene.view.camera_position, glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f });
    // 显式用 RH_ZO：Vulkan 的深度范围是 [0,1]，不受 GLM_FORCE_DEPTH_ZERO_TO_ONE 影响。
    scene.view.projection = glm::perspectiveRH_ZO(glm::radians(60.0f), aspect, 0.1f, 100.0f);
    // Vulkan 的 NDC Y 向下，翻一下。绕向随之反转，见 ForwardOpaquePass 的光栅化状态。
    scene.view.projection[1][1] *= -1.0f;

    rendering::DrawItem draw;
    draw.mesh = m_cube_mesh;
    draw.material = m_cube_material;
    draw.transform = glm::rotate(glm::mat4{ 1.0f }, m_elapsed_seconds * 0.8f,
            glm::normalize(glm::vec3{ 0.3f, 1.0f, 0.2f }));
    scene.draws.push_back(draw);

    const auto statistics = m_renderer->renderFrame(scene);
    if (m_frame_index == 1 && statistics.rendered) {
        core::Application::get().getLogChannel().info(
                "First frame rendered ({} draw calls)", statistics.draw_calls);
    }
}

} // namespace arti::sample
