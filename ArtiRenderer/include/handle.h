#pragma once
#include "artichoco/core/uuid.h"

#include <cstddef>
#include <functional>
#include <string>

namespace arti::rendering {


template<typename Tag>
class Handle {
public:
    using Value = core::UUID::Value;

    constexpr Handle() noexcept = default;

    explicit constexpr Handle(core::UUID uuid) noexcept
            : m_uuid(uuid) {}

    static Handle generate() { return Handle{ core::UUID::generate() }; }

    constexpr core::UUID uuid() const noexcept { return m_uuid; }

    constexpr Value value() const noexcept { return m_uuid.value(); }

    constexpr bool isValid() const noexcept { return m_uuid.isValid(); }

    std::string toString() const { return m_uuid.toString(); }

    constexpr auto operator<=>(const Handle&) const noexcept = default;

private:
    core::UUID m_uuid;
};

struct TextureTag;
struct MeshTag;
struct MaterialTag;

using TextureHandle = Handle<TextureTag>;
using MeshHandle = Handle<MeshTag>;
using MaterialHandle = Handle<MaterialTag>;

} // namespace arti::rendering

namespace std {

template<typename Tag>
struct hash<arti::rendering::Handle<Tag>> {
    size_t operator()(arti::rendering::Handle<Tag> handle) const noexcept {
        return hash<arti::core::UUID>{}(handle.uuid());
    }
};

} // namespace std
