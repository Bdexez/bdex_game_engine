#include <bdex/assets/Mesh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace bdex::assets {

MeshData makeCube(f32 size) {
    struct Face {
        Vec3 normal;
        Vec3 right;  // +U direction on the face
        Vec3 up;     // +V direction on the face; right x up == normal
    };
    static const std::array<Face, 6> faces = {{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};

    const f32 half = size * 0.5f;
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    for (const Face& face : faces) {
        const u32 base = static_cast<u32>(mesh.vertices.size());
        const Vec3 center = face.normal * half;
        const Vec3 right = face.right * half;
        const Vec3 up = face.up * half;
        // Counter-clockwise when seen from outside (right x up = normal).
        mesh.vertices.push_back({center - right - up, face.normal, {0.0f, 1.0f}});
        mesh.vertices.push_back({center + right - up, face.normal, {1.0f, 1.0f}});
        mesh.vertices.push_back({center + right + up, face.normal, {1.0f, 0.0f}});
        mesh.vertices.push_back({center - right + up, face.normal, {0.0f, 0.0f}});
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

MeshData makePlane(f32 size, f32 uvScale) {
    const f32 half = size * 0.5f;
    const Vec3 normal(0.0f, 1.0f, 0.0f);
    MeshData mesh;
    mesh.vertices = {
        {{-half, 0.0f, half}, normal, {0.0f, uvScale}},
        {{half, 0.0f, half}, normal, {uvScale, uvScale}},
        {{half, 0.0f, -half}, normal, {uvScale, 0.0f}},
        {{-half, 0.0f, -half}, normal, {0.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

MeshData makeSphere(f32 radius, u32 segments, u32 rings) {
    segments = std::max(segments, 3u);
    rings = std::max(rings, 2u);
    const f32 pi = std::numbers::pi_v<f32>;

    MeshData mesh;
    mesh.vertices.reserve(static_cast<usize>(rings + 1) * (segments + 1));
    mesh.indices.reserve(static_cast<usize>(rings) * segments * 6);

    // Rings go from the north pole (theta = 0) to the south pole.
    for (u32 ring = 0; ring <= rings; ++ring) {
        const f32 v = static_cast<f32>(ring) / static_cast<f32>(rings);
        const f32 theta = v * pi;
        for (u32 segment = 0; segment <= segments; ++segment) {
            const f32 u = static_cast<f32>(segment) / static_cast<f32>(segments);
            const f32 phi = u * 2.0f * pi;
            const Vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta),
                              std::sin(theta) * std::sin(phi));
            mesh.vertices.push_back({normal * radius, normal, {u, v}});
        }
    }

    const u32 stride = segments + 1;
    for (u32 ring = 0; ring < rings; ++ring) {
        for (u32 segment = 0; segment < segments; ++segment) {
            const u32 a = ring * stride + segment;  // upper ring
            const u32 b = a + stride;               // lower ring
            // Skip the degenerate triangles at the poles.
            if (ring != 0) {
                mesh.indices.insert(mesh.indices.end(), {a, a + 1, b});
            }
            if (ring != rings - 1) {
                mesh.indices.insert(mesh.indices.end(), {a + 1, b + 1, b});
            }
        }
    }
    return mesh;
}

}  // namespace bdex::assets
