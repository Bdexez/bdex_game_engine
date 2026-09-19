#pragma once

#include <bdex/core/Handle.hpp>
#include <bdex/core/Math.hpp>

namespace bdex::render {

// Handles to resources owned by the renderer service.
using MeshHandle = Handle<struct MeshTag>;
using TextureHandle = Handle<struct TextureTag>;
using MaterialHandle = Handle<struct MaterialTag>;

// --- ECS components understood by renderers -----------------------------------
// They are plain data: any renderer module can interpret them, and gameplay
// code never depends on a particular renderer.

/// Perspective camera. The view is taken from the entity's WorldTransform
/// (looking down -Z). The first camera flagged `primary` is used.
struct Camera {
    f32 verticalFov = glm::radians(60.0f);
    f32 nearPlane = 0.1f;
    f32 farPlane = 500.0f;
    bool primary = true;
};

/// Draws a mesh with a material at the entity's WorldTransform.
struct MeshRenderer {
    MeshHandle mesh;
    MaterialHandle material;
    bool visible = true;
};

/// Sun-like light. `direction` is the direction the light travels in.
struct DirectionalLight {
    Vec3 direction{-0.4f, -1.0f, -0.3f};
    Vec3 color{1.0f};
    f32 intensity = 1.0f;
    Vec3 ambient{0.08f};
};

}  // namespace bdex::render
