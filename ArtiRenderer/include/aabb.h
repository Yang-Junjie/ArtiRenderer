#pragma once

#include <glm/glm.hpp>

#include <limits>

namespace arti::rendering {

struct AABB {
    glm::vec3 min{ std::numeric_limits<float>::max() };
    glm::vec3 max{ std::numeric_limits<float>::lowest() };

    bool isEmpty() const noexcept { return min.x > max.x || min.y > max.y || min.z > max.z; }

    glm::vec3 center() const noexcept { return (min + max) * 0.5f; }

    glm::vec3 extents() const noexcept { return (max - min) * 0.5f; }

    void expand(const glm::vec3& point) noexcept {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    void merge(const AABB& other) noexcept {
        if (other.isEmpty()) {
            return;
        }
        expand(other.min);
        expand(other.max);
    }

    AABB transformed(const glm::mat4& transform) const noexcept {
        if (isEmpty()) {
            return *this;
        }

        AABB result;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 point{
                (corner & 1) != 0 ? max.x : min.x,
                (corner & 2) != 0 ? max.y : min.y,
                (corner & 4) != 0 ? max.z : min.z,
            };
            result.expand(glm::vec3{ transform * glm::vec4{ point, 1.0f } });
        }
        return result;
    }
};

} // namespace arti::rendering
