#include "shadow_targets.h"

#include "log.h"

#include <stdexcept>
#include <string>

namespace arti::rendering {
namespace {

// 和 SceneDepth 同一个格式。32 位深度对阴影是必要的：D16 在「近平面卡得不够紧」的时候
// acne 会明显得多，而 near/far 的收紧是逐帧算出来的，不能假定它总是最优。
constexpr auto shadowDepthFormat = nvrhi::Format::D32;

} // namespace

void ShadowTargets::prepare(nvrhi::IDevice& device) {
    if (m_depth_array) {
        return;
    }

    nvrhi::TextureDesc desc;
    desc.setWidth(kShadowMapResolution)
            .setHeight(kShadowMapResolution)
            .setArraySize(kShadowCascadeCount)
            .setDimension(nvrhi::TextureDimension::Texture2DArray)
            .setFormat(shadowDepthFormat)
            .setIsRenderTarget(true)
            .setDebugName("ArtiRenderer ShadowCascades")
            .enableAutomaticStateTracking(nvrhi::ResourceStates::DepthWrite);
    m_depth_array = device.createTexture(desc);
    if (!m_depth_array) {
        throw std::runtime_error("NVRHI failed to create the shadow cascade depth array.");
    }

    // 每级一个 framebuffer，只挂深度、没有颜色附件。
    //
    // 「0 个颜色附件」在 FramebufferDesc 里是能表达的（colorAttachments 是 static_vector），
    // 但后端接不接受是这一步要实测的第一件事。建不出来的退路是挂一张同尺寸的 R8 假颜色附件
    // —— 走那条路要先在任务文档的交接区记一条，别默默加。
    for (uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        nvrhi::FramebufferDesc framebuffer_desc;
        framebuffer_desc.setDepthAttachment(
                nvrhi::FramebufferAttachment().setTexture(m_depth_array).setArraySlice(cascade));
        m_framebuffers[cascade] = device.createFramebuffer(framebuffer_desc);
        if (!m_framebuffers[cascade]) {
            throw std::runtime_error("NVRHI failed to create the shadow framebuffer for cascade " +
                                     std::to_string(cascade) + ".");
        }
    }

    ++m_revision;
    getLogChannel().debug("Shadow cascades created: {} x {}^2 (revision {})", kShadowCascadeCount,
            kShadowMapResolution, m_revision);
}

nvrhi::ITexture& ShadowTargets::depthArray() const {
    if (!m_depth_array) {
        throw std::logic_error("ShadowTargets::depthArray() before prepare().");
    }
    return *m_depth_array;
}

nvrhi::IFramebuffer& ShadowTargets::framebuffer(uint32_t cascade) const {
    if (cascade >= kShadowCascadeCount) {
        throw std::out_of_range("Shadow cascade index " + std::to_string(cascade) +
                                " is out of range.");
    }
    if (!m_framebuffers[cascade]) {
        throw std::logic_error("ShadowTargets::framebuffer() before prepare().");
    }
    return *m_framebuffers[cascade];
}

} // namespace arti::rendering
