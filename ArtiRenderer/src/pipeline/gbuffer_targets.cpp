#include "gbuffer_targets.h"

#include "log.h"

#include <stdexcept>

namespace arti::rendering {
namespace {

// albedo 是个 [0,1] 的反射率，所以 8 位够用 —— 但走 sRGB 而不是线性 UNORM：8 位线性在暗部
// 会有可见带状，借硬件的 sRGB 编码等于免费拿到感知均匀的精度分布。alpha 通道不受 sRGB 影响
// （Vulkan 规定只编码 RGB），所以存在里面的 metallic 是线性的。
constexpr auto albedoMetallicFormat = nvrhi::Format::SRGBA8_UNORM;
// 法线要 16F：8 位法线在大平面上会出现可见的分层（尤其配上镜面高光）。roughness 挤在 w 里
// 顺便也拿到了 16 位精度，虽然它并不需要。
constexpr auto normalRoughnessFormat = nvrhi::Format::RGBA16_FLOAT;
// emissive 是场景线性 HDR，emissive_strength 可以远超 1.0，8 位归一化会在写入时就削顶。
constexpr auto emissiveOcclusionFormat = nvrhi::Format::RGBA16_FLOAT;

// 和 RenderTargetSet 的离屏目标一样只带 render target 用途，不开 isUAV：跟 compute 无关的
// 目标不该付 storage usage 的代价（某些硬件会因此放弃 framebuffer 压缩）。真要 compute
// 着色（tiled / clustered）时再加，那是建纹理时的标志、事后加不了。
nvrhi::TextureDesc makeDesc(uint32_t width, uint32_t height, nvrhi::Format format,
        const char* debug_name) {
    nvrhi::TextureDesc desc;
    desc.setWidth(width)
            .setHeight(height)
            .setFormat(format)
            .setIsRenderTarget(true)
            .setDebugName(debug_name)
            .enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);
    return desc;
}

} // namespace

void GBufferTargets::prepare(nvrhi::IDevice& device, const RenderTargetSet& targets) {
    if (!targets.isReady()) {
        throw std::logic_error("GBufferTargets::prepare() before RenderTargetSet::prepare().");
    }
    if (m_framebuffer && m_source_revision == targets.revision()) {
        return;
    }

    const auto& scene_info = targets.sceneFramebuffer().getFramebufferInfo();
    const auto width = scene_info.width;
    const auto height = scene_info.height;

    m_albedo_metallic = device.createTexture(
            makeDesc(width, height, albedoMetallicFormat, "ArtiRenderer GBufferAlbedoMetallic"));
    m_normal_roughness = device.createTexture(
            makeDesc(width, height, normalRoughnessFormat, "ArtiRenderer GBufferNormalRoughness"));
    m_emissive_occlusion = device.createTexture(makeDesc(width, height, emissiveOcclusionFormat,
            "ArtiRenderer GBufferEmissiveOcclusion"));
    if (!m_albedo_metallic || !m_normal_roughness || !m_emissive_occlusion) {
        throw std::runtime_error("NVRHI failed to create the G-Buffer textures.");
    }

    // 附件顺序就是 shader 里 SV_Target0/1/2 的顺序，改这里必须同时改 gbuffer.slang。
    // 深度借 RenderTargetSet 的那张。
    nvrhi::FramebufferDesc framebuffer_desc;
    framebuffer_desc.addColorAttachment(m_albedo_metallic)
            .addColorAttachment(m_normal_roughness)
            .addColorAttachment(m_emissive_occlusion)
            .setDepthAttachment(&targets.sceneDepth());
    m_framebuffer = device.createFramebuffer(framebuffer_desc);
    if (!m_framebuffer) {
        throw std::runtime_error("NVRHI failed to create the G-Buffer framebuffer.");
    }

    m_source_revision = targets.revision();
    ++m_revision;
    getLogChannel().debug("G-Buffer rebuilt at {}x{} (revision {})", width, height, m_revision);
}

nvrhi::ITexture& GBufferTargets::albedoMetallic() const {
    if (!m_albedo_metallic) {
        throw std::logic_error("GBufferTargets::albedoMetallic() before prepare().");
    }
    return *m_albedo_metallic;
}

nvrhi::ITexture& GBufferTargets::normalRoughness() const {
    if (!m_normal_roughness) {
        throw std::logic_error("GBufferTargets::normalRoughness() before prepare().");
    }
    return *m_normal_roughness;
}

nvrhi::ITexture& GBufferTargets::emissiveOcclusion() const {
    if (!m_emissive_occlusion) {
        throw std::logic_error("GBufferTargets::emissiveOcclusion() before prepare().");
    }
    return *m_emissive_occlusion;
}

nvrhi::IFramebuffer& GBufferTargets::framebuffer() const {
    if (!m_framebuffer) {
        throw std::logic_error("GBufferTargets::framebuffer() before prepare().");
    }
    return *m_framebuffer;
}

} // namespace arti::rendering
