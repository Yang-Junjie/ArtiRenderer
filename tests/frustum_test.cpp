#include "frustum.h"
#include "test_check.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace {

// 以原点为相机、朝 -Z 看（glm 的右手约定）。所以「前方」是 z 负、「后方」是 z 正。
constexpr float kNear = 0.1f;
constexpr float kFar = 100.0f;

// 正交那组 —— 数量级刻意跟透视拉开：近平面在 z=-1，所以「近平面写错」这件事在正交下
// 是几十个单位的位移，不是透视那种 0.05 的缝。
constexpr float kOrthoNear = 1.0f;
constexpr float kOrthoFar = 100.0f;
constexpr float kOrthoExtent = 20.0f;

arti::rendering::AABB boxAt(const glm::vec3& center, float half_size)
{
    arti::rendering::AABB box;
    box.expand(center - glm::vec3{ half_size });
    box.expand(center + glm::vec3{ half_size });
    return box;
}

} // namespace

int main()
{
    using arti::rendering::AABB;
    using arti::rendering::Frustum;

    arti::test::Checker checker{ "frustum_test" };

    // 本工程的投影一律是 _ZO 变体，测试必须跟着用同一个 —— 用 NO 建矩阵再拿 ZO 的公式提取，
    // 测的就不是生产代码走的那条路了。
    const glm::mat4 projection =
            glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, kNear, kFar);
    const glm::mat4 view = glm::lookAtRH(
            glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 0.0f, -1.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f });
    const Frustum frustum = Frustum::fromViewProjection(projection * view);

    // 六个平面都该是单位法线，否则保守测试里那个「点到平面距离」会按平面各自缩放偏。
    for (std::size_t index = 0; index < frustum.planes.size(); ++index) {
        const float length = glm::length(glm::vec3{ frustum.planes[index] });
        ARTI_CHECK(checker, std::abs(length - 1.0f) < 1e-4f);
    }

    // 1. 正前方的小盒子可见。
    ARTI_CHECK(checker, frustum.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, -10.0f }, 1.0f)));

    // 2. 相机正后方的盒子必须被剔掉。
    //    注意这条**抓不住**近平面写成 NO 公式（row3 + row2）那个 bug：透视矩阵下两种公式
    //    都朝前，NO 只是把近平面从 -0.1 挪到 -0.05，背后的东西照样剔。真正抓它的是下面
    //    的正交用例 —— 实测过，只留透视用例的话反向验证不会变红。
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, 10.0f }, 1.0f)));
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, 50.0f }, 1.0f)));

    // 3. 远平面之外被剔掉。
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, -(kFar + 20.0f) }, 1.0f)));

    // 4. 左右上下各出一个明显在外面的。距离取 10，横向偏移取 50 —— 60° 垂直 FOV、16:9 下
    //    z=-10 处的半宽约 10.3、半高约 5.8，偏 50 稳稳在外面。
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ -50.0f, 0.0f, -10.0f }, 1.0f)));
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ 50.0f, 0.0f, -10.0f }, 1.0f)));
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ 0.0f, -50.0f, -10.0f }, 1.0f)));
    ARTI_CHECK(checker, !frustum.intersects(boxAt(glm::vec3{ 0.0f, 50.0f, -10.0f }, 1.0f)));

    // 5. 横跨近平面的大盒子**可见** —— 保守测试不许剔掉部分相交的东西。
    //    盒子中心在原点（相机位置），半边长 5，所以它同时跨过近平面和四个侧面。
    ARTI_CHECK(checker, frustum.intersects(boxAt(glm::vec3{ 0.0f }, 5.0f)));

    // 6. 空 AABB 不可见。
    ARTI_CHECK(checker, !frustum.intersects(AABB{}));

    // 7. 近平面朝内：相机前方一点在内侧、后方一点在外侧。直接断平面本身，
    //    这样近平面的符号错误在「哪个平面错了」这个层面就能定位，不用靠盒子测试反推。
    const glm::vec4 near_plane = frustum.planes[4];
    const auto signedDistance = [&near_plane](const glm::vec3& point) {
        return glm::dot(glm::vec3{ near_plane }, point) + near_plane.w;
    };
    ARTI_CHECK(checker, signedDistance(glm::vec3{ 0.0f, 0.0f, -10.0f }) > 0.0f);
    ARTI_CHECK(checker, signedDistance(glm::vec3{ 0.0f, 0.0f, 10.0f }) < 0.0f);

    // 8. 默认构造的 Frustum 六个平面全零 —— 约定它不剔任何东西（除了空盒）。
    //    忘了初始化时的症状是「没有剔除」，而不是「画面全黑」。
    const Frustum uninitialized;
    ARTI_CHECK(checker, uninitialized.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, 10.0f }, 1.0f)));
    ARTI_CHECK(checker, !uninitialized.intersects(AABB{}));

    // 9. **正交投影** —— 阴影 cascade 走的就是这条（shadow_cascades.cpp 的 orthoRH_ZO）。
    //    正交的 row3 是常量 (0,0,0,1)，近平面公式写错的后果不是「挪一条缝」而是整条平面
    //    翻到 z=+98 去，几乎什么都不剔。这一组是近平面公式的**唯一**有效防线。
    const glm::mat4 ortho_projection = glm::orthoRH_ZO(
            -kOrthoExtent, kOrthoExtent, -kOrthoExtent, kOrthoExtent, kOrthoNear, kOrthoFar);
    const Frustum ortho = Frustum::fromViewProjection(ortho_projection * view);

    for (std::size_t index = 0; index < ortho.planes.size(); ++index) {
        const float length = glm::length(glm::vec3{ ortho.planes[index] });
        ARTI_CHECK(checker, std::abs(length - 1.0f) < 1e-4f);
    }

    // 9a. 盒体在正交近平面之内、侧面之内 → 可见。
    ARTI_CHECK(checker, ortho.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, -10.0f }, 1.0f)));

    // 9b. 相机后方 —— ZO 下 z<=-1 才在内侧，所以 z≈+50 必须被剔。
    //     写成 NO 的话近平面变成 z<=+98，这个盒子会被判可见，这条断言就红。
    ARTI_CHECK(checker, !ortho.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, 50.0f }, 1.0f)));

    // 9c. 只差一点点：中心 z=+3、半边长 1 → z∈[2,4]，整体在近平面后面。
    //     NO 公式下同样会漏过去。
    ARTI_CHECK(checker, !ortho.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, 3.0f }, 1.0f)));

    // 9d. 正交没有透视收缩，侧面就是 ±kOrthoExtent 两块竖直平面 —— 顺手确认一下没提取错。
    ARTI_CHECK(checker,
            !ortho.intersects(boxAt(glm::vec3{ kOrthoExtent + 5.0f, 0.0f, -10.0f }, 1.0f)));
    ARTI_CHECK(checker, ortho.intersects(boxAt(glm::vec3{ kOrthoExtent - 5.0f, 0.0f, -10.0f }, 1.0f)));

    // 9e. 远平面之外。
    ARTI_CHECK(checker,
            !ortho.intersects(boxAt(glm::vec3{ 0.0f, 0.0f, -(kOrthoFar + 20.0f) }, 1.0f)));

    // 9f. 正交近平面的符号，跟第 7 条同样的道理，直接断平面本身。
    const glm::vec4 ortho_near_plane = ortho.planes[4];
    const auto orthoSignedDistance = [&ortho_near_plane](const glm::vec3& point) {
        return glm::dot(glm::vec3{ ortho_near_plane }, point) + ortho_near_plane.w;
    };
    ARTI_CHECK(checker, orthoSignedDistance(glm::vec3{ 0.0f, 0.0f, -10.0f }) > 0.0f);
    ARTI_CHECK(checker, orthoSignedDistance(glm::vec3{ 0.0f, 0.0f, 10.0f }) < 0.0f);
    // 近平面精确落在 z=-kOrthoNear：距离应当正好是 0。
    ARTI_CHECK(checker,
            std::abs(orthoSignedDistance(glm::vec3{ 0.0f, 0.0f, -kOrthoNear })) < 1e-4f);

    return checker.summary();
}
