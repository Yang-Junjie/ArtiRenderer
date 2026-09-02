#include "shadow_cascades.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace arti::rendering::detail {
namespace {

// 分割比例，相对 shadow_distance。抄 Godot 的默认值（split_1/2/3 = 0.1/0.2/0.5），
// 那套参数得看画面调，而它已经在无数项目上调过了。
constexpr std::array<float, kShadowCascadeCount> kSplitRatios{ 0.1f, 0.2f, 0.5f, 1.0f };

// 光源的 view 基。光方向是**传播方向**（和 LightDesc 的约定一致），所以视线就是它。
glm::mat4 lightView(const glm::vec3& direction) {
    // up 不能和光方向平行，否则 lookAt 的叉积退化。太阳接近正上方时是常见情形，所以必须处理。
    const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3{ 0.0f, 0.0f, 1.0f }
                                                      : glm::vec3{ 0.0f, 1.0f, 0.0f };
    return glm::lookAtRH(glm::vec3{ 0.0f }, direction, up);
}

// 从投影矩阵取半视角的 tan。perspectiveRH_ZO 下 projection[1][1] = 1 / tan(fovY/2)，
// projection[0][0] = 1 / (aspect * tan(fovY/2))，所以直接取倒数就行 —— 不需要知道 fov 和 aspect
// 各是多少，也不受 near / far 影响。
//
// 注意本项目**不做** projection[1][1] 的符号翻转（NVRHI 的 Vulkan 后端用负 viewport 高度），
// 所以这两个值都是正的。
struct HalfExtents {
    float tan_x{ 0.0f };
    float tan_y{ 0.0f };
};

HalfExtents halfExtentsOf(const glm::mat4& projection) {
    HalfExtents extents;
    if (std::abs(projection[0][0]) > 1e-6f) {
        extents.tan_x = 1.0f / std::abs(projection[0][0]);
    }
    if (std::abs(projection[1][1]) > 1e-6f) {
        extents.tan_y = 1.0f / std::abs(projection[1][1]);
    }
    return extents;
}

} // namespace

ShadowCascadeResult computeShadowCascades(const RenderView& view, const LightDesc& light,
        const std::vector<DrawItem>& draws) {
    ShadowCascadeResult result;

    // 阴影只覆盖到 shadow_distance —— 相机远平面可能在几百米外，让阴影图去摊那个范围的话
    // 近处一个 texel 能盖好几厘米。
    const float far_limit = std::max(view.near_plane + 1e-3f,
            std::min(view.far_plane, light.shadow_distance));
    result.shadow_distance = far_limit;

    const glm::vec3 direction = glm::length(light.direction) > 0.0f
            ? glm::normalize(light.direction)
            : glm::vec3{ 0.0f, -1.0f, 0.0f };
    const glm::mat4 light_view = lightView(direction);

    // 相机基。view 是世界到相机，逆过来就是相机的世界矩阵：列 0/1/2 是 right / up / -forward。
    const glm::mat4 camera_world = glm::inverse(view.view);
    const glm::vec3 camera_right{ camera_world[0] };
    const glm::vec3 camera_up{ camera_world[1] };
    const glm::vec3 camera_forward{ -glm::vec3{ camera_world[2] } };
    const glm::vec3 camera_position{ camera_world[3] };

    const HalfExtents extents = halfExtentsOf(view.projection);

    // 投影体的候选：每个 draw 在光空间的 AABB。near / far 收紧要用它们，逐级重算太贵，
    // 所以先算一遍存下来。
    struct LightSpaceBounds {
        glm::vec3 min{ 0.0f };
        glm::vec3 max{ 0.0f };
    };
    std::vector<LightSpaceBounds> caster_bounds;
    caster_bounds.reserve(draws.size());
    for (const auto& draw: draws) {
        LightSpaceBounds bounds;
        bool first = true;
        // AABB 的八个角都要变换：光空间是旋转过的，只变换 min/max 两个点会得到错的盒子。
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 world{
                (corner & 1) != 0 ? draw.world_bounds.max.x : draw.world_bounds.min.x,
                (corner & 2) != 0 ? draw.world_bounds.max.y : draw.world_bounds.min.y,
                (corner & 4) != 0 ? draw.world_bounds.max.z : draw.world_bounds.min.z,
            };
            const glm::vec3 light_space{ light_view * glm::vec4{ world, 1.0f } };
            bounds.min = first ? light_space : glm::min(bounds.min, light_space);
            bounds.max = first ? light_space : glm::max(bounds.max, light_space);
            first = false;
        }
        caster_bounds.push_back(bounds);
    }

    float split_near = view.near_plane;
    for (uint32_t index = 0; index < kShadowCascadeCount; ++index) {
        const float split_far = view.near_plane + (far_limit - view.near_plane) * kSplitRatios[index];

        // 这一级子视锥的八个角，世界空间。
        std::array<glm::vec3, 8> corners{};
        std::size_t next = 0;
        for (const float distance: { split_near, split_far }) {
            const glm::vec3 center = camera_position + camera_forward * distance;
            const glm::vec3 half_x = camera_right * (extents.tan_x * distance);
            const glm::vec3 half_y = camera_up * (extents.tan_y * distance);
            corners[next++] = center - half_x - half_y;
            corners[next++] = center + half_x - half_y;
            corners[next++] = center - half_x + half_y;
            corners[next++] = center + half_x + half_y;
        }

        // 用**包围球**而不是光空间 AABB 定正交范围。
        //
        // 这是 texel 取整能不能真的稳定的关键：AABB 的大小随相机朝向变化（转一下相机，
        // 同一个视锥在光空间的投影盒就变形了），范围一变，取整的基准也变，边缘照样闪。
        // 球是旋转不变的，所以范围大小逐帧恒定，只有球心在动 —— 那才是「把球心按 texel 取整」
        // 能消掉 shimmering 的前提。
        //
        // 代价是拟合更松（球比盒大），有效分辨率降一些。这是标准做法里公认的取舍。
        glm::vec3 world_center{ 0.0f };
        for (const auto& corner: corners) {
            world_center += corner;
        }
        world_center /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const auto& corner: corners) {
            radius = std::max(radius, glm::length(corner - world_center));
        }
        // 向上取到一个稳定的量级：半径本身会有浮点抖动，直接用它会让范围每帧差一点点。
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const glm::vec3 light_center{ light_view * glm::vec4{ world_center, 1.0f } };

        // texel 取整：把球心按「一个 texel 等于多少世界单位」对齐。
        const float diameter = radius * 2.0f;
        const float units_per_texel = diameter / static_cast<float>(kShadowMapResolution);
        glm::vec2 snapped{ light_center.x, light_center.y };
        if (units_per_texel > 0.0f) {
            snapped.x = std::floor(snapped.x / units_per_texel) * units_per_texel;
            snapped.y = std::floor(snapped.y / units_per_texel) * units_per_texel;
        }

        const float min_x = snapped.x - radius;
        const float max_x = snapped.x + radius;
        const float min_y = snapped.y - radius;
        const float max_y = snapped.y + radius;

        // near / far：只看**在 XY 上和这一级重叠**的投影体。
        //
        // 比拿整个场景 AABB 的 Z 范围紧得多（MSDN 那份文档里「naive」的做法就是后者，
        // 最坏情况能差四倍），而又比正经的「把场景几何裁剪到光锥四个侧面」便宜得多。
        // near 必须比这一级的盒子更靠光源一侧 —— 挡在中间的物体也得画进去，否则它不投影。
        float min_z = light_center.z - radius;
        float max_z = light_center.z + radius;
        for (const auto& bounds: caster_bounds) {
            if (bounds.max.x < min_x || bounds.min.x > max_x || bounds.max.y < min_y ||
                    bounds.min.y > max_y) {
                continue;
            }
            min_z = std::min(min_z, bounds.min.z);
            max_z = std::max(max_z, bounds.max.z);
        }

        // 光空间是右手系、视线朝 -Z，所以「离光源更近」= z 更大。正交的 near / far 取的是
        // **到 -z 方向的距离**，于是 near = -max_z、far = -min_z。
        const float near_plane = -max_z;
        const float far_plane = -min_z;

        auto& cascade = result.cascades[index];
        cascade.light_view_projection =
                glm::orthoRH_ZO(min_x, max_x, min_y, max_y, near_plane, far_plane) * light_view;
        cascade.split_far = split_far;
        cascade.world_units_per_texel = units_per_texel;


        split_near = split_far;
    }

    return result;
}

} // namespace arti::rendering::detail
