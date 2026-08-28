#pragma once

#include <bdex/core/Math.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <string_view>
#include <vector>

namespace bdex::assets {

struct Vertex {
    Vec3 position{0.0f};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Vec2 uv{0.0f};
};

/// CPU-side triangle mesh (counter-clockwise front faces). Renderer-agnostic:
/// a renderer uploads it and hands back its own handle.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<u32> indices;

    [[nodiscard]] usize triangleCount() const { return indices.size() / 3; }
};

// --- Procedural primitives, centered on the origin ----------------------------

/// Axis-aligned cube with per-face normals and UVs.
[[nodiscard]] MeshData makeCube(f32 size = 1.0f);
/// Square in the XZ plane facing +Y. `uvScale` tiles the texture.
[[nodiscard]] MeshData makePlane(f32 size = 1.0f, f32 uvScale = 1.0f);
/// UV sphere.
[[nodiscard]] MeshData makeSphere(f32 radius = 0.5f, u32 segments = 32, u32 rings = 16);

// --- Loading -------------------------------------------------------------------

/// Parses Wavefront OBJ text (positions, normals, UVs, polygons triangulated
/// as fans, negative indices). Missing normals are computed by averaging
/// face normals.
[[nodiscard]] Result<MeshData> parseObj(std::string_view text);

}  // namespace bdex::assets
