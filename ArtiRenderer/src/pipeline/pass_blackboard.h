#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace arti::rendering {


enum class PassSlot : uint8_t {
    SceneColor,
    SceneDepth,
    Count,
};

std::string_view toString(PassSlot slot) noexcept;

// pass 之间交接纹理用的黑板。
//
// 只存裸指针：纹理的所有权在产出它的那个 pass 手里（nvrhi::TextureHandle），
// 生命周期覆盖整帧。黑板只是借来指一下，每帧开头 reset()。
//
// 重要：RenderDevice::renderFrame 是先跑完所有 pass 的 prepare()，再跑所有
// record()，两个阶段是分开的批次。所以登记必须发生在 prepare() 里，
// 在 record() 里 set 的话下游永远读不到。
class PassBlackboard {
public:
    void set(PassSlot slot, nvrhi::ITexture& texture) noexcept;

    nvrhi::ITexture* find(PassSlot slot) const noexcept;

    // 缺槽位时直接抛（带 slot 名），让它当场炸而不是画出一片黑。
    nvrhi::ITexture& require(PassSlot slot) const;

    void reset() noexcept;

private:
    std::array<nvrhi::ITexture*, static_cast<size_t>(PassSlot::Count)> m_textures{};
};

} // namespace arti::rendering
