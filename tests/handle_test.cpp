#include "handle.h"
#include "test_check.h"

#include <unordered_map>

namespace {

template <typename A, typename B>
concept ComparableAcross = requires(A a, B b) { a == b; };

// 回归测试：以前 TextureHandle/MeshHandle 都从 core::UUID 公开继承，
// 继承来的 operator<=> 会把两边切片到基类，跨类型比较能悄悄编译通过。
static_assert(ComparableAcross<arti::rendering::TextureHandle, arti::rendering::TextureHandle>);
static_assert(!ComparableAcross<arti::rendering::TextureHandle, arti::rendering::MeshHandle>);
static_assert(!ComparableAcross<arti::rendering::MeshHandle, arti::rendering::MaterialHandle>);

// 同样的原因，std::hash<UUID> 的特化当年不适用于派生类，
// 导致 unordered_map<TextureHandle, T> 编译失败。
static_assert(sizeof(arti::rendering::TextureHandle) == sizeof(arti::core::UUID));

} // namespace

int main()
{
    using arti::rendering::MeshHandle;
    using arti::rendering::TextureHandle;

    arti::test::Checker checker{ "handle_test" };

    ARTI_CHECK(checker, !TextureHandle{}.isValid());

    const auto first = TextureHandle::generate();
    const auto second = TextureHandle::generate();
    ARTI_CHECK(checker, first.isValid());
    ARTI_CHECK(checker, first != second);
    ARTI_CHECK(checker, first == TextureHandle{ first.uuid() });

    std::unordered_map<TextureHandle, int> textures;
    textures.emplace(first, 1);
    textures.emplace(second, 2);
    ARTI_CHECK(checker, textures.size() == 2);
    ARTI_CHECK(checker, textures.at(first) == 1);
    ARTI_CHECK(checker, textures.find(TextureHandle{}) == textures.end());

    std::unordered_map<MeshHandle, int> meshes;
    meshes.emplace(MeshHandle::generate(), 7);
    ARTI_CHECK(checker, meshes.size() == 1);

    return checker.summary();
}
