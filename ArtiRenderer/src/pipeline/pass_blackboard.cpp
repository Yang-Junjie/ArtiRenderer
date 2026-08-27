#include "pass_blackboard.h"

#include <stdexcept>
#include <string>

namespace arti::rendering {

std::string_view toString(PassSlot slot) noexcept
{
    switch (slot) {
        case PassSlot::SceneColor:
            return "SceneColor";
        case PassSlot::SceneDepth:
            return "SceneDepth";
        case PassSlot::Count:
            break;
    }
    return "<invalid>";
}

void PassBlackboard::set(PassSlot slot, nvrhi::ITexture& texture) noexcept
{
    const auto index = static_cast<size_t>(slot);
    if (index >= m_textures.size()) {
        return;
    }
    m_textures[index] = &texture;
}

nvrhi::ITexture* PassBlackboard::find(PassSlot slot) const noexcept
{
    const auto index = static_cast<size_t>(slot);
    return index < m_textures.size() ? m_textures[index] : nullptr;
}

nvrhi::ITexture& PassBlackboard::require(PassSlot slot) const
{
    if (auto* texture = find(slot)) {
        return *texture;
    }
    throw std::runtime_error(
            "No pass published the blackboard slot '" + std::string{ toString(slot) } + "'.");
}

void PassBlackboard::reset() noexcept
{
    m_textures.fill(nullptr);
}

} // namespace arti::rendering
