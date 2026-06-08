#pragma once

// Single entry point for math types. The GLM configuration macros
// (radians, [0, 1] clip-space depth) are set by the bdex_core CMake target so
// that every library and plugin agrees on the conventions.
//
// Engine conventions: right-handed, +Y up, -Z forward, counter-clockwise
// front faces.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace bdex {

using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;
using Quat = glm::quat;

}  // namespace bdex
