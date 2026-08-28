#include <bdex/assets/Mesh.hpp>

#include <charconv>
#include <map>
#include <tuple>

namespace bdex::assets {
namespace {

std::string_view nextToken(std::string_view& line) {
    constexpr std::string_view whitespace = " \t\r";
    const usize begin = line.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        line = {};
        return {};
    }
    const usize end = line.find_first_of(whitespace, begin);
    const std::string_view token = line.substr(begin, end - begin);
    line = end == std::string_view::npos ? std::string_view{} : line.substr(end);
    return token;
}

template <class T>
bool parseNumber(std::string_view text, T& out) {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

bool parseFloats(std::string_view& line, f32* out, int count) {
    for (int i = 0; i < count; ++i) {
        if (!parseNumber(nextToken(line), out[i])) {
            return false;
        }
    }
    return true;
}

/// Converts a 1-based (or negative, relative) OBJ index to 0-based.
/// Returns -1 when absent, -2 when out of range.
i64 resolveIndex(std::string_view text, usize count) {
    if (text.empty()) {
        return -1;
    }
    i64 value = 0;
    if (!parseNumber(text, value) || value == 0) {
        return -2;
    }
    const i64 index = value > 0 ? value - 1 : static_cast<i64>(count) + value;
    return index >= 0 && index < static_cast<i64>(count) ? index : -2;
}

}  // namespace

Result<MeshData> parseObj(std::string_view text) {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;

    MeshData mesh;
    // (position, uv, normal) -> index of the merged vertex
    std::map<std::tuple<i64, i64, i64>, u32> lookup;
    bool missingNormals = false;
    usize lineNumber = 0;

    while (!text.empty()) {
        ++lineNumber;
        const usize newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

        const std::string_view keyword = nextToken(line);
        if (keyword.empty() || keyword.front() == '#') {
            continue;
        }

        if (keyword == "v") {
            f32 v[3];
            if (!parseFloats(line, v, 3)) {
                return fail("obj line {}: malformed vertex position", lineNumber);
            }
            positions.emplace_back(v[0], v[1], v[2]);
        } else if (keyword == "vn") {
            f32 v[3];
            if (!parseFloats(line, v, 3)) {
                return fail("obj line {}: malformed normal", lineNumber);
            }
            normals.emplace_back(v[0], v[1], v[2]);
        } else if (keyword == "vt") {
            f32 v[2];
            if (!parseFloats(line, v, 2)) {
                return fail("obj line {}: malformed texture coordinate", lineNumber);
            }
            // OBJ has V pointing up; the engine samples with V pointing down.
            uvs.emplace_back(v[0], 1.0f - v[1]);
        } else if (keyword == "f") {
            std::vector<u32> corners;
            for (std::string_view corner = nextToken(line); !corner.empty();
                 corner = nextToken(line)) {
                // v, v/vt, v//vn or v/vt/vn
                const usize firstSlash = corner.find('/');
                const usize secondSlash = firstSlash == std::string_view::npos
                                              ? std::string_view::npos
                                              : corner.find('/', firstSlash + 1);
                const std::string_view positionText = corner.substr(0, firstSlash);
                const std::string_view uvText =
                    firstSlash == std::string_view::npos
                        ? std::string_view{}
                        : corner.substr(firstSlash + 1, secondSlash == std::string_view::npos
                                                            ? std::string_view::npos
                                                            : secondSlash - firstSlash - 1);
                const std::string_view normalText = secondSlash == std::string_view::npos
                                                        ? std::string_view{}
                                                        : corner.substr(secondSlash + 1);

                const i64 p = resolveIndex(positionText, positions.size());
                const i64 t = resolveIndex(uvText, uvs.size());
                const i64 n = resolveIndex(normalText, normals.size());
                if (p < 0 || t == -2 || n == -2) {
                    return fail("obj line {}: invalid face index '{}'", lineNumber, corner);
                }

                const auto key = std::make_tuple(p, t, n);
                auto it = lookup.find(key);
                if (it == lookup.end()) {
                    Vertex vertex;
                    vertex.position = positions[static_cast<usize>(p)];
                    vertex.uv = t >= 0 ? uvs[static_cast<usize>(t)] : Vec2(0.0f);
                    vertex.normal = n >= 0 ? normals[static_cast<usize>(n)] : Vec3(0.0f);
                    missingNormals = missingNormals || n < 0;
                    it = lookup.emplace(key, static_cast<u32>(mesh.vertices.size())).first;
                    mesh.vertices.push_back(vertex);
                }
                corners.push_back(it->second);
            }
            if (corners.size() < 3) {
                return fail("obj line {}: face with fewer than 3 vertices", lineNumber);
            }
            for (usize i = 1; i + 1 < corners.size(); ++i) {
                mesh.indices.insert(mesh.indices.end(), {corners[0], corners[i], corners[i + 1]});
            }
        }
        // Other statements (o, g, s, usemtl, mtllib...) do not affect geometry.
    }

    if (mesh.indices.empty()) {
        return fail("obj: no faces found");
    }

    if (missingNormals) {
        // Area-weighted average of the adjacent face normals.
        std::vector<Vec3> accumulated(mesh.vertices.size(), Vec3(0.0f));
        for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const u32 a = mesh.indices[i];
            const u32 b = mesh.indices[i + 1];
            const u32 c = mesh.indices[i + 2];
            const Vec3 faceNormal =
                glm::cross(mesh.vertices[b].position - mesh.vertices[a].position,
                           mesh.vertices[c].position - mesh.vertices[a].position);
            accumulated[a] += faceNormal;
            accumulated[b] += faceNormal;
            accumulated[c] += faceNormal;
        }
        for (usize i = 0; i < mesh.vertices.size(); ++i) {
            if (mesh.vertices[i].normal == Vec3(0.0f)) {
                const f32 length = glm::length(accumulated[i]);
                mesh.vertices[i].normal =
                    length > 0.0f ? accumulated[i] / length : Vec3(0.0f, 1.0f, 0.0f);
            }
        }
    }
    return mesh;
}

}  // namespace bdex::assets
