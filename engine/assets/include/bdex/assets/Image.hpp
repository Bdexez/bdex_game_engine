#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <array>
#include <filesystem>
#include <span>
#include <vector>

namespace bdex::assets {

using Color8 = std::array<u8, 4>;  ///< R, G, B, A

/// CPU-side image: tightly packed RGBA8, rows from top to bottom.
struct ImageData {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> pixels;

    [[nodiscard]] bool valid() const {
        return width != 0 && height != 0 &&
               pixels.size() == static_cast<usize>(width) * height * 4;
    }
};

// --- Procedural images ---------------------------------------------------------

[[nodiscard]] ImageData makeSolidImage(u32 width, u32 height, Color8 color);
/// `cells` x `cells` checkerboard alternating between two colors.
[[nodiscard]] ImageData makeCheckerboard(u32 size, u32 cells, Color8 first, Color8 second);

// --- Decoding / encoding ---------------------------------------------------------

/// Decodes a TGA file (true-color 24/32-bit, raw or RLE).
[[nodiscard]] Result<ImageData> decodeTga(std::span<const u8> bytes);

/// Encodes to PNG. The stream is stored uncompressed, which keeps the encoder
/// dependency-free; it is meant for captures and debugging output.
[[nodiscard]] std::vector<u8> encodePng(const ImageData& image);
[[nodiscard]] Result<> writePng(const std::filesystem::path& path, const ImageData& image);

}  // namespace bdex::assets
