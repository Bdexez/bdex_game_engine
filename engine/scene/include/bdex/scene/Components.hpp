#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/ecs/Entity.hpp>

#include <string>

namespace bdex::scene {

/// Human-readable label (debugging, tools).
struct Name {
    std::string value;
};

/// Position, rotation and scale relative to the parent (or to the world when
/// the entity has no Parent).
struct Transform {
    Vec3 position{0.0f};
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    Vec3 scale{1.0f};

    [[nodiscard]] Mat4 localMatrix() const {
        Mat4 matrix = glm::translate(Mat4(1.0f), position) * glm::mat4_cast(rotation);
        return glm::scale(matrix, scale);
    }

    /// Unit vectors of the local frame, in parent space. Forward is -Z.
    [[nodiscard]] Vec3 forward() const { return rotation * Vec3(0.0f, 0.0f, -1.0f); }
    [[nodiscard]] Vec3 right() const { return rotation * Vec3(1.0f, 0.0f, 0.0f); }
    [[nodiscard]] Vec3 up() const { return rotation * Vec3(0.0f, 1.0f, 0.0f); }
};

/// Attaches the entity's Transform to another entity's.
struct Parent {
    ecs::Entity entity;
};

/// Local-to-world matrix, derived from Transform (and Parent) by
/// updateWorldTransforms(). Read-only for gameplay code.
struct WorldTransform {
    Mat4 matrix{1.0f};
};

}  // namespace bdex::scene
