#include <bdex/rhi/Device.hpp>
#include <bdex/rhi/Types.hpp>

#include <gtest/gtest.h>

#include <vector>

using namespace bdex;
using namespace bdex::rhi;

TEST(RhiTypes, UsageFlagsCombine) {
    const TextureUsage usage = TextureUsage::Sampled | TextureUsage::TransferSrc;
    EXPECT_TRUE(hasFlag(usage, TextureUsage::Sampled));
    EXPECT_TRUE(hasFlag(usage, TextureUsage::TransferSrc));
    EXPECT_FALSE(hasFlag(usage, TextureUsage::ColorAttachment));
    EXPECT_FALSE(hasFlag(usage, TextureUsage::None));  // "none" is never set

    BufferUsage buffer = BufferUsage::Vertex;
    buffer |= BufferUsage::TransferDst;
    EXPECT_TRUE(hasFlag(buffer, BufferUsage::TransferDst));
}

TEST(RhiTypes, FormatQueries) {
    EXPECT_TRUE(isDepthFormat(Format::D32Float));
    EXPECT_FALSE(isDepthFormat(Format::RGBA8Srgb));
    EXPECT_TRUE(hasStencil(Format::D24UnormS8Uint));
    EXPECT_FALSE(hasStencil(Format::D32Float));
    EXPECT_EQ(bytesPerPixel(Format::BGRA8Unorm), 4u);
    EXPECT_EQ(bytesPerPixel(Format::RGBA16Float), 8u);
    EXPECT_EQ(formatName(Format::BGRA8Srgb), "BGRA8Srgb");
}

TEST(RhiTypes, FullMipChainLength) {
    EXPECT_EQ(fullMipChainLength(1, 1), 1u);
    EXPECT_EQ(fullMipChainLength(2, 2), 2u);
    EXPECT_EQ(fullMipChainLength(256, 256), 9u);
    EXPECT_EQ(fullMipChainLength(256, 16), 9u);   // driven by the largest side
    EXPECT_EQ(fullMipChainLength(300, 200), 9u);  // 300 -> 150 -> ... -> 1
}

TEST(RhiTypes, AsBytesViewsValuesSpansAndVectors) {
    struct Pair {
        u32 a;
        u32 b;
    };
    const Pair pair{1, 2};
    EXPECT_EQ(asBytes(pair).size(), sizeof(Pair));

    const std::vector<u32> values = {1, 2, 3};
    EXPECT_EQ(asBytes(values).size(), 3 * sizeof(u32));
    EXPECT_EQ(asBytes(std::span<const u32>(values)).size(), 3 * sizeof(u32));
    EXPECT_EQ(static_cast<const void*>(asBytes(values).data()),
              static_cast<const void*>(values.data()));
}
