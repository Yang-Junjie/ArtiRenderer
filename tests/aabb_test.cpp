#include "aabb.h"
#include "test_check.h"

#include <glm/gtc/matrix_transform.hpp>

int main()
{
    using arti::rendering::AABB;

    arti::test::Checker checker{ "aabb_test" };

    // 默认构造是「空盒」，这样 expand 不用特判第一个点。
    AABB box;
    ARTI_CHECK(checker, box.isEmpty());

    box.expand(glm::vec3{ 1.0f, 2.0f, 3.0f });
    ARTI_CHECK(checker, !box.isEmpty());
    ARTI_CHECK(checker, box.min == glm::vec3(1.0f, 2.0f, 3.0f));
    ARTI_CHECK(checker, box.max == glm::vec3(1.0f, 2.0f, 3.0f));

    box.expand(glm::vec3{ -1.0f, 0.0f, 5.0f });
    ARTI_CHECK(checker, box.min == glm::vec3(-1.0f, 0.0f, 3.0f));
    ARTI_CHECK(checker, box.max == glm::vec3(1.0f, 2.0f, 5.0f));
    ARTI_CHECK(checker, box.center() == glm::vec3(0.0f, 1.0f, 4.0f));
    ARTI_CHECK(checker, box.extents() == glm::vec3(1.0f, 1.0f, 1.0f));

    // 合并空盒不该污染结果。
    AABB merged = box;
    merged.merge(AABB{});
    ARTI_CHECK(checker, merged.min == box.min && merged.max == box.max);

    // 平移后的包围盒应当整体跟着走。
    const auto moved = box.transformed(glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 10.0f }));
    ARTI_CHECK(checker, moved.min == box.min + glm::vec3(10.0f));
    ARTI_CHECK(checker, moved.max == box.max + glm::vec3(10.0f));

    // 空盒变换后仍是空盒。
    ARTI_CHECK(checker, AABB{}.transformed(glm::mat4{ 1.0f }).isEmpty());

    return checker.summary();
}
