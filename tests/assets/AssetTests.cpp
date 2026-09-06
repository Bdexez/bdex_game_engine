#include <bdex/assets/AssetManager.hpp>
#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/FileSystem.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace bdex;
using namespace bdex::assets;

namespace {

/// True when every triangle is wound counter-clockwise as seen from outside,
/// i.e. its geometric normal agrees with the vertex normals.
bool windingMatchesNormals(const MeshData& mesh) {
    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[i]];
        const Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
        if (glm::dot(faceNormal, a.normal + b.normal + c.normal) <= 0.0f) {
            return false;
        }
    }
    return true;
}

u32 readBigEndian(const std::vector<u8>& bytes, usize offset) {
    return (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
           (static_cast<u32>(bytes[offset + 2]) << 8) | bytes[offset + 3];
}

/// Temporary directory removed at scope exit.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("bdex_tests_" + std::to_string(std::hash<std::string>{}(
                                    ::testing::UnitTest::GetInstance()->current_test_info()->name())));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    void write(const std::string& name, const std::string& content) const {
        std::ofstream(path / name, std::ios::binary) << content;
    }
};

}  // namespace

// --- Primitives ----------------------------------------------------------------

TEST(Primitives, CubeHasPerFaceVerticesAndOutwardWinding) {
    const MeshData cube = makeCube(2.0f);
    EXPECT_EQ(cube.vertices.size(), 24u);
    EXPECT_EQ(cube.triangleCount(), 12u);
    EXPECT_TRUE(windingMatchesNormals(cube));
    for (const Vertex& vertex : cube.vertices) {
        EXPECT_FLOAT_EQ(std::abs(vertex.position.x), 1.0f);
        EXPECT_FLOAT_EQ(glm::length(vertex.normal), 1.0f);
    }
}

TEST(Primitives, PlaneFacesUp) {
    const MeshData plane = makePlane(10.0f, 4.0f);
    EXPECT_EQ(plane.triangleCount(), 2u);
    EXPECT_TRUE(windingMatchesNormals(plane));
    EXPECT_FLOAT_EQ(plane.vertices[1].uv.x, 4.0f);
}

TEST(Primitives, SphereIsClosedAndOutwardFacing) {
    const u32 segments = 16;
    const u32 rings = 8;
    const MeshData sphere = makeSphere(0.5f, segments, rings);
    EXPECT_EQ(sphere.vertices.size(), static_cast<usize>((segments + 1) * (rings + 1)));
    // Two triangles per quad, minus the degenerate ones at both poles.
    EXPECT_EQ(sphere.triangleCount(), static_cast<usize>(segments * (rings - 1) * 2));
    EXPECT_TRUE(windingMatchesNormals(sphere));
    for (const Vertex& vertex : sphere.vertices) {
        EXPECT_NEAR(glm::length(vertex.position), 0.5f, 1e-5f);
    }
}

// --- OBJ -------------------------------------------------------------------------

TEST(ObjLoader, TriangulatesPolygonsAndMergesVertices) {
    const auto mesh = parseObj(R"(
# a unit quad
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1 4/4/1
)");
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_EQ(mesh->vertices.size(), 4u);  // shared corners are not duplicated
    EXPECT_EQ(mesh->indices, (std::vector<u32>{0, 1, 2, 0, 2, 3}));
    EXPECT_EQ(mesh->vertices[2].normal, Vec3(0.0f, 0.0f, 1.0f));
    // V is flipped: OBJ's origin is bottom-left, the engine's is top-left.
    EXPECT_EQ(mesh->vertices[0].uv, Vec2(0.0f, 1.0f));
    EXPECT_EQ(mesh->vertices[2].uv, Vec2(1.0f, 0.0f));
}

TEST(ObjLoader, SupportsNegativeIndicesAndComputesMissingNormals) {
    const auto mesh = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n");
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    ASSERT_EQ(mesh->vertices.size(), 3u);
    for (const Vertex& vertex : mesh->vertices) {
        EXPECT_EQ(vertex.normal, Vec3(0.0f, 0.0f, 1.0f));  // CCW in the XY plane => +Z
    }
}

TEST(ObjLoader, ReportsErrorsWithLineNumbers) {
    const auto badIndex = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 9\n");
    ASSERT_FALSE(badIndex.has_value());
    EXPECT_NE(badIndex.error().message.find("line 4"), std::string::npos);

    EXPECT_FALSE(parseObj("v 0 0\n").has_value());         // malformed position
    EXPECT_FALSE(parseObj("v 0 0 0\nv 1 0 0\n").has_value());  // no faces
}

// --- Images ----------------------------------------------------------------------

TEST(Image, CheckerboardAlternatesCells) {
    const Color8 white{255, 255, 255, 255};
    const Color8 black{0, 0, 0, 255};
    const ImageData image = makeCheckerboard(4, 2, white, black);
    ASSERT_TRUE(image.valid());
    const auto pixel = [&](u32 x, u32 y) { return image.pixels[(y * 4 + x) * 4]; };
    EXPECT_EQ(pixel(0, 0), 255);
    EXPECT_EQ(pixel(2, 0), 0);
    EXPECT_EQ(pixel(0, 2), 0);
    EXPECT_EQ(pixel(3, 3), 255);
}

TEST(Image, DecodesRawAndRleTga) {
    // 2x2, 24-bit, bottom-left origin. Pixels are stored as BGR.
    const std::vector<u8> header = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 0, 24, 0};
    std::vector<u8> raw = header;
    raw.insert(raw.end(), {0, 0, 255, /* bottom-left: red */ 0, 255, 0, /* bottom-right: green */
                           255, 0, 0, /* top-left: blue */ 255, 255, 255 /* top-right: white */});

    const auto decoded = decodeTga(raw);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    ASSERT_TRUE(decoded->valid());
    // Rows are stored top to bottom in ImageData.
    EXPECT_EQ((std::vector<u8>(decoded->pixels.begin(), decoded->pixels.begin() + 8)),
              (std::vector<u8>{0, 0, 255, 255, 255, 255, 255, 255}));
    EXPECT_EQ((std::vector<u8>(decoded->pixels.begin() + 8, decoded->pixels.end())),
              (std::vector<u8>{255, 0, 0, 255, 0, 255, 0, 255}));

    // Same size, RLE (type 10), top-left origin, one run of 4 orange pixels.
    std::vector<u8> rle = header;
    rle[2] = 10;
    rle[17] = 0x20;
    rle.insert(rle.end(), {0x83, 0, 128, 255});
    const auto run = decodeTga(rle);
    ASSERT_TRUE(run.has_value()) << run.error().message;
    for (usize i = 0; i < run->pixels.size(); i += 4) {
        EXPECT_EQ(run->pixels[i], 255);
        EXPECT_EQ(run->pixels[i + 1], 128);
        EXPECT_EQ(run->pixels[i + 2], 0);
        EXPECT_EQ(run->pixels[i + 3], 255);
    }

    EXPECT_FALSE(decodeTga(std::vector<u8>(4, 0)).has_value());                     // too small
    EXPECT_FALSE(decodeTga(std::vector<u8>(header.begin(), header.end())).has_value());  // truncated
}

TEST(Image, EncodesWellFormedPng) {
    const ImageData image = makeCheckerboard(300, 3, {255, 0, 0, 255}, {0, 0, 255, 255});
    const std::vector<u8> png = encodePng(image);

    const std::vector<u8> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    ASSERT_GT(png.size(), 33u);
    EXPECT_TRUE(std::equal(signature.begin(), signature.end(), png.begin()));

    // IHDR is the first chunk: length 13, then width and height.
    EXPECT_EQ(readBigEndian(png, 8), 13u);
    EXPECT_EQ(std::string(png.begin() + 12, png.begin() + 16), "IHDR");
    EXPECT_EQ(readBigEndian(png, 16), 300u);
    EXPECT_EQ(readBigEndian(png, 20), 300u);
    EXPECT_EQ(png[24], 8);  // bit depth
    EXPECT_EQ(png[25], 6);  // RGBA

    // The file ends with an empty IEND chunk.
    EXPECT_EQ(std::string(png.end() - 8, png.end() - 4), "IEND");

    // 300 * (300 * 4 + 1) bytes of scanlines need several stored blocks
    // (65535 bytes max each): the payload must still be accounted for exactly.
    const usize rawSize = 300u * (300u * 4u + 1u);
    const usize blocks = (rawSize + 65534) / 65535;
    const usize idatSize = 2 + blocks * 5 + rawSize + 4;
    EXPECT_EQ(readBigEndian(png, 33), idatSize);
}

// --- AssetManager ------------------------------------------------------------------

TEST(AssetManager, LoadsThroughTheVirtualFileSystemAndCaches) {
    const TempDir dir;
    dir.write("triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);

    const auto first = assets.load<MeshData>("data:/triangle.obj");
    ASSERT_TRUE(first.has_value()) << first.error().message;
    EXPECT_EQ((*first)->triangleCount(), 1u);

    const auto second = assets.load<MeshData>("data:/triangle.obj");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->get(), second->get());  // same instance: served from the cache
    EXPECT_EQ(assets.cachedCount(), 1u);
}

TEST(AssetManager, ReportsMissingFilesLoadersAndMounts) {
    const TempDir dir;
    dir.write("notes.txt", "hello");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);

    EXPECT_FALSE(assets.load<MeshData>("data:/missing.obj").has_value());
    EXPECT_FALSE(assets.load<MeshData>("nowhere:/triangle.obj").has_value());

    const auto noLoader = assets.load<MeshData>("data:/notes.txt");
    ASSERT_FALSE(noLoader.has_value());
    EXPECT_NE(noLoader.error().message.find(".txt"), std::string::npos);
}

TEST(AssetManager, AcceptsCustomLoaders) {
    struct Note {
        std::string text;
    };
    const TempDir dir;
    dir.write("notes.txt", "hello");

    VirtualFileSystem vfs;
    vfs.mount("data", dir.path);
    AssetManager assets(vfs);
    assets.registerLoader<Note>(".txt", [](std::span<const u8> bytes) -> Result<Note> {
        return Note{std::string(bytes.begin(), bytes.end())};
    });

    const auto note = assets.load<Note>("data:/notes.txt");
    ASSERT_TRUE(note.has_value()) << note.error().message;
    EXPECT_EQ((*note)->text, "hello");
}
