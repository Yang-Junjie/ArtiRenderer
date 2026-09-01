#include "gbuffer_pass.h"

#include "artichoco/renderer/slang_compiler.h"
#include "artichoco/renderer/vulkan/nvrhi_shader_factory.h"
#include "draw_resolve.h"
#include "log.h"
#include "mesh_vertex_layout.h"
#include "nvrhi_conversion.h"
#include "shader_paths.h"

#include <array>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace arti::rendering {
namespace {

// push constant。model(64) + 三行法线矩阵(48) = 112，留在 Vulkan 保证的 128 以内。
//
// 传法线矩阵而不是让着色器用 model 的左上 3x3：TransformComponent 允许非等比缩放，那种情况下
// 后者是错的。材质参数因此挤不进 push constant，改走逐材质的 uniform buffer —— 反正它是按材质
// 而不是按 draw 变的，放进 push constant 本来就是浪费。
struct GBufferDrawConstants {
    std::array<float, 16> model;
    std::array<float, 4> normal_row0;
    std::array<float, 4> normal_row1;
    std::array<float, 4> normal_row2;
};

static_assert(std::is_standard_layout_v<GBufferDrawConstants>);
static_assert(sizeof(GBufferDrawConstants) == sizeof(float) * 28);
static_assert(sizeof(GBufferDrawConstants) <= 128,
        "Vulkan only guarantees 128 push constant bytes.");

// 逐帧常量。延迟管线下几何 pass 只需要相机矩阵 —— 光源、环境光、IBL 全在光照 pass 那边，
// 所以这个 struct 只剩一个矩阵。
struct GBufferFrameConstants {
    std::array<float, 16> view_projection;
};

static_assert(std::is_standard_layout_v<GBufferFrameConstants>);
static_assert(sizeof(GBufferFrameConstants) == sizeof(float) * 16);

// 逐材质常量。全部 float4，HLSL 与 std140 的打包规则因此一致，两侧不用对 padding 猜谜。
struct GBufferMaterialConstants {
    std::array<float, 4> base_color;
    // x = metallic, y = roughness, z = occlusion 强度, w = emissive 强度
    std::array<float, 4> factors;
};

static_assert(std::is_standard_layout_v<GBufferMaterialConstants>);
static_assert(sizeof(GBufferMaterialConstants) == sizeof(float) * 8);

// 材质引用的五张贴图。绑定集按 MaterialHandle 缓存，而 updateMaterial() 可以换掉贴图，
// 所以要记下建集时用的那一组，变了就重建 —— 只比句柄，不碰 GPU 资源。
struct MaterialTextures {
    TextureHandle base_color;
    TextureHandle metallic_roughness;
    TextureHandle normal;
    TextureHandle occlusion;
    TextureHandle emissive;

    bool operator==(const MaterialTextures&) const = default;
};

MaterialTextures texturesOf(const Material& material, TextureHandle flat_normal) {
    MaterialTextures textures;
    textures.base_color = material.base_color_texture;
    textures.metallic_roughness = material.metallic_roughness_texture;
    // 法线槽的兜底不是白图：白图解出来是 (1,1,1) 的歪法线。内建 flat normal 图解出来是
    // (0,0,1)，也就是「用几何法线」，所以在这里替掉，着色器里就不需要分支。
    textures.normal = material.normal_texture.isValid() ? material.normal_texture : flat_normal;
    textures.occlusion = material.occlusion_texture;
    textures.emissive = material.emissive_texture;
    return textures;
}

GBufferMaterialConstants constantsOf(const Material& material) {
    GBufferMaterialConstants constants{};
    std::memcpy(constants.base_color.data(), glm::value_ptr(material.base_color),
            sizeof(constants.base_color));
    constants.factors = { material.metallic_strength, material.roughness_strength,
        material.occlusion_strength, material.emissive_strength };
    return constants;
}

} // namespace

struct GBufferPass::Impl {
    arti::renderer::ShaderReflection reflection;
    nvrhi::ShaderHandle vertex_shader;
    nvrhi::ShaderHandle pixel_shader;
    nvrhi::BindingLayoutHandle binding_layout;
    nvrhi::InputLayoutHandle input_layout;
    nvrhi::SamplerHandle sampler;

    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::FramebufferInfo pipeline_framebuffer_info;

    // 逐帧常量，每帧 writeBuffer 一次，所有 draw 共用。非 volatile，理由见创建处。
    nvrhi::BufferHandle frame_constants;

    struct MaterialResources {
        nvrhi::BufferHandle constants;
        nvrhi::BindingSetHandle binding_set;
        MaterialTextures textures;
    };
    std::unordered_map<MaterialHandle, MaterialResources> materials;

    // 注意这里**没有**环境 revision 那套失效逻辑：IBL 三件套已经搬去光照 pass 了，
    // 所以重烘环境不再需要把每个材质的绑定集全部推倒重建 —— 这是延迟化顺带拿到的简化。
    MaterialResources& resourcesFor(PassRecordContext& context, MaterialHandle handle,
            const Material& material) {
        auto& device = context.device();
        const auto textures = texturesOf(material, context.frame().resources().flatNormalTexture());

        auto& entry = materials[handle];
        if (!entry.constants) {
            nvrhi::BufferDesc desc;
            desc.setByteSize(sizeof(GBufferMaterialConstants))
                    .setIsConstantBuffer(true)
                    .setDebugName("ArtiRenderer GBufferMaterialConstants")
                    .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
            entry.constants = device.createBuffer(desc);
            if (!entry.constants) {
                throw std::runtime_error(
                        "NVRHI failed to create a G-Buffer material constant buffer.");
            }
        }
        if (!entry.binding_set || entry.textures != textures) {
            const std::array resources = {
                arti::renderer::vulkan::NvrhiBindingResource::Buffer("frame_constants",
                        *frame_constants),
                arti::renderer::vulkan::NvrhiBindingResource::Buffer("material_constants",
                        *entry.constants),
                arti::renderer::vulkan::NvrhiBindingResource::Texture("base_color_texture",
                        context.texture(textures.base_color)),
                arti::renderer::vulkan::NvrhiBindingResource::Texture("metallic_roughness_texture",
                        context.texture(textures.metallic_roughness)),
                arti::renderer::vulkan::NvrhiBindingResource::Texture("normal_texture",
                        context.texture(textures.normal)),
                arti::renderer::vulkan::NvrhiBindingResource::Texture("occlusion_texture",
                        context.texture(textures.occlusion)),
                arti::renderer::vulkan::NvrhiBindingResource::Texture("emissive_texture",
                        context.texture(textures.emissive)),
                arti::renderer::vulkan::NvrhiBindingResource::Sampler("material_sampler", *sampler),
            };
            entry.binding_set = arti::renderer::vulkan::createNvrhiBindingSet(device, reflection, 0,
                    *binding_layout, resources);
            if (!entry.binding_set) {
                throw std::runtime_error("NVRHI failed to create a G-Buffer binding set.");
            }
            entry.textures = textures;
        }
        return entry;
    }
};

GBufferPass::GBufferPass()
        : m_impl(std::make_unique<Impl>()) {}

GBufferPass::~GBufferPass() = default;

void GBufferPass::prepare(PassPrepareContext& context) {
    auto& device = context.device();

    if (!m_impl->binding_layout) {
        const auto program = arti::renderer::SlangCompiler::compileGraphics(
                { detail::shaderPath("gbuffer.slang") });
        const auto shaders = arti::renderer::vulkan::createNvrhiGraphicsShaderSet(device, program,
                "ArtiRenderer G-Buffer");
        if (shaders.binding_layouts.empty() || !shaders.binding_layouts.front()) {
            throw std::runtime_error("The G-Buffer shader has no NVRHI binding layout.");
        }
        m_impl->vertex_shader = shaders.vertex_shader;
        m_impl->pixel_shader = shaders.pixel_shader;
        m_impl->binding_layout = shaders.binding_layouts.front();
        m_impl->reflection = program.reflection;

        // glTF 的默认 wrapS/wrapT 是 REPEAT，贴图 UV 越界很常见，所以按它来。
        nvrhi::SamplerDesc sampler_desc;
        sampler_desc.setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Repeat);
        m_impl->sampler = device.createSampler(sampler_desc);
        if (!m_impl->sampler) {
            throw std::runtime_error("NVRHI failed to create the G-Buffer sampler.");
        }

        // 刻意**不用** volatile：binding layout 是从 shader 反射来的，反射看不到 buffer 是不是
        // volatile，只能发出普通 ConstantBuffer，而 BindingSetItem 会从 buffer 自动判定成
        // VolatileConstantBuffer，两者不一致 Vulkan 会报 descriptor type 错误。
        nvrhi::BufferDesc frame_desc;
        frame_desc.setByteSize(sizeof(GBufferFrameConstants))
                .setIsConstantBuffer(true)
                .setDebugName("ArtiRenderer GBufferFrameConstants")
                .enableAutomaticStateTracking(nvrhi::ResourceStates::ConstantBuffer);
        m_impl->frame_constants = device.createBuffer(frame_desc);
        if (!m_impl->frame_constants) {
            throw std::runtime_error("NVRHI failed to create the G-Buffer frame constant buffer.");
        }

        const auto attributes = detail::toNvrhiAttributes(detail::meshVertexLayout());
        m_impl->input_layout = device.createInputLayout(attributes.data(),
                static_cast<uint32_t>(attributes.size()), m_impl->vertex_shader);
        if (!m_impl->input_layout) {
            throw std::runtime_error("NVRHI failed to create the G-Buffer input layout.");
        }
    }

    const auto& framebuffer_info = context.gbuffer().framebuffer().getFramebufferInfo();
    if (!m_impl->pipeline || m_impl->pipeline_framebuffer_info !=
                                     static_cast<const nvrhi::FramebufferInfo&>(framebuffer_info)) {
        nvrhi::DepthStencilState depth_state;
        depth_state.enableDepthTest().enableDepthWrite().disableStencil();
        nvrhi::RasterState raster_state;
        // 网格按「从外面看逆时针 = 正面」的常规约定编写。PickingPass 用的是同一个值 ——
        // 它重画同一批几何来解拾取，约定不一致会让「点到的」和「看到的」对不上。
        raster_state.setCullBack().setFrontCounterClockwise(true);
        nvrhi::RenderState render_state;
        // 混合状态用默认值（三个附件全部关混合、全通道写入）：G-Buffer 存的是属性而不是
        // 光能，混合在这里没有意义。
        render_state.setDepthStencilState(depth_state).setRasterState(raster_state);

        nvrhi::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
                .setInputLayout(m_impl->input_layout)
                .setVertexShader(m_impl->vertex_shader)
                .setPixelShader(m_impl->pixel_shader)
                .setRenderState(render_state)
                .addBindingLayout(m_impl->binding_layout);
        m_impl->pipeline = device.createGraphicsPipeline(pipeline_desc, framebuffer_info);
        if (!m_impl->pipeline) {
            throw std::runtime_error("NVRHI failed to create the G-Buffer graphics pipeline.");
        }
        m_impl->pipeline_framebuffer_info = framebuffer_info;
        getLogChannel().debug("Created the G-Buffer graphics pipeline");
    }
}

void GBufferPass::record(PassRecordContext& context) {
    auto& frame = context.frame();
    const auto& scene = frame.scene();
    auto& commands = context.commands();

    auto& framebuffer = context.gbuffer().framebuffer();
    nvrhi::ViewportState viewport;
    viewport.addViewportAndScissorRect(framebuffer.getFramebufferInfo().getViewport());

    // 逐帧常量写一次，所有 draw 共用。
    const glm::mat4 view_projection = scene.view.projection * scene.view.view;
    GBufferFrameConstants frame_constants{};
    std::memcpy(frame_constants.view_projection.data(), glm::value_ptr(view_projection),
            sizeof(frame_constants.view_projection));
    commands.writeBuffer(m_impl->frame_constants, &frame_constants, sizeof(frame_constants));

    // 先把本帧要用到的材质常量全部写好，再进绘制循环。分两趟是为了不在 setGraphicsState 之间
    // 插 writeBuffer —— 那会让命令列表在绘制中途做资源状态转换。
    //
    // 每帧重写而不是建的时候写一次：updateMaterial() 可以改材质参数，重写是最省心的正确做法，
    // 几十个材质的小 buffer 写入可以忽略。
    std::unordered_set<MaterialHandle> written;
    for (const auto& draw: scene.draws) {
        const auto resolved = detail::resolveDraw(frame, draw);
        if (!resolved || resolved->material.type != MaterialType::PBR) {
            continue;
        }
        if (!written.insert(draw.material).second) {
            continue;
        }
        auto& entry = m_impl->resourcesFor(context, draw.material, resolved->material);
        const auto constants = constantsOf(resolved->material);
        commands.writeBuffer(entry.constants, &constants, sizeof(constants));
    }

    for (const auto& draw: scene.draws) {
        const auto resolved = detail::resolveDraw(frame, draw);
        if (!resolved || resolved->material.type != MaterialType::PBR) {
            continue;
        }

        GBufferDrawConstants constants{};
        std::memcpy(constants.model.data(), glm::value_ptr(draw.transform),
                sizeof(constants.model));
        // glm 是列主序，normal_matrix[col][row]；着色器按行做 dot，所以这里按行取。
        const glm::mat3 normal_matrix = glm::inverseTranspose(glm::mat3{ draw.transform });
        constants.normal_row0 = { normal_matrix[0][0], normal_matrix[1][0], normal_matrix[2][0],
            0.0f };
        constants.normal_row1 = { normal_matrix[0][1], normal_matrix[1][1], normal_matrix[2][1],
            0.0f };
        constants.normal_row2 = { normal_matrix[0][2], normal_matrix[1][2], normal_matrix[2][2],
            0.0f };

        auto& entry = m_impl->resourcesFor(context, draw.material, resolved->material);

        nvrhi::GraphicsState state;
        state.setPipeline(m_impl->pipeline)
                .setFramebuffer(&framebuffer)
                .setViewport(viewport)
                .addBindingSet(entry.binding_set)
                .addVertexBuffer(nvrhi::VertexBufferBinding()
                                .setBuffer(&context.vertexBuffer(draw.mesh))
                                .setSlot(0))
                .setIndexBuffer(nvrhi::IndexBufferBinding()
                                .setBuffer(&context.indexBuffer(draw.mesh))
                                .setFormat(detail::toNvrhiIndexFormat(
                                        resolved->mesh->index_buffer.indexType())));
        commands.setGraphicsState(state);
        commands.setPushConstants(&constants, sizeof(constants));
        commands.drawIndexed(nvrhi::DrawArguments{}
                        .setVertexCount(resolved->submesh->index_count)
                        .setStartIndexLocation(resolved->submesh->index_offset)
                        .setStartVertexLocation(resolved->submesh->vertex_offset));

        ++frame.statistics().draw_calls;
        ++frame.statistics().submeshes;
    }
}

} // namespace arti::rendering
