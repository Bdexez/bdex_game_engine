#include <bdex/assets/Image.hpp>

#include <bdex/core/FileSystem.hpp>

#include <algorithm>
#include <cstring>

namespace bdex::assets {

ImageData makeSolidImage(u32 width, u32 height, Color8 color) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<usize>(width) * height * 4);
    for (usize i = 0; i < image.pixels.size(); i += 4) {
        std::copy(color.begin(), color.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return image;
}

ImageData makeCheckerboard(u32 size, u32 cells, Color8 first, Color8 second) {
    cells = std::max(cells, 1u);
    ImageData image = makeSolidImage(size, size, first);
    for (u32 y = 0; y < size; ++y) {
        for (u32 x = 0; x < size; ++x) {
            const u32 cellX = x * cells / size;
            const u32 cellY = y * cells / size;
            if ((cellX + cellY) % 2 == 1) {
                std::copy(second.begin(), second.end(),
                          image.pixels.begin() +
                              static_cast<std::ptrdiff_t>((static_cast<usize>(y) * size + x) * 4));
            }
        }
    }
    return image;
}

// --- TGA -------------------------------------------------------------------------

Result<ImageData> decodeTga(std::span<const u8> bytes) {
    constexpr usize kHeaderSize = 18;
    if (bytes.size() < kHeaderSize) {
        return fail("tga: file too small");
    }

    const u8 idLength = bytes[0];
    const u8 colorMapType = bytes[1];
    const u8 imageType = bytes[2];
    const u32 width = bytes[12] | (static_cast<u32>(bytes[13]) << 8);
    const u32 height = bytes[14] | (static_cast<u32>(bytes[15]) << 8);
    const u8 bitsPerPixel = bytes[16];
    const bool topToBottom = (bytes[17] & 0x20) != 0;

    if (colorMapType != 0 || (imageType != 2 && imageType != 10)) {
        return fail("tga: only true-color images are supported (type {})", imageType);
    }
    if (bitsPerPixel != 24 && bitsPerPixel != 32) {
        return fail("tga: unsupported bit depth {}", bitsPerPixel);
    }
    if (width == 0 || height == 0) {
        return fail("tga: empty image");
    }

    const usize bytesPerPixel = bitsPerPixel / 8;
    const usize pixelCount = static_cast<usize>(width) * height;
    usize cursor = kHeaderSize + idLength;

    ImageData image;
    image.width = width;
    image.height = height;
    image.pixels.resize(pixelCount * 4);

    // Writes pixel `index` (in file order) from BGR(A) source bytes.
    const auto store = [&](usize index, const u8* source) {
        const usize row = index / width;
        const usize column = index % width;
        const usize targetRow = topToBottom ? row : height - 1 - row;
        u8* target = image.pixels.data() + (targetRow * width + column) * 4;
        target[0] = source[2];
        target[1] = source[1];
        target[2] = source[0];
        target[3] = bytesPerPixel == 4 ? source[3] : 255;
    };

    if (imageType == 2) {
        if (bytes.size() < cursor + pixelCount * bytesPerPixel) {
            return fail("tga: truncated pixel data");
        }
        for (usize i = 0; i < pixelCount; ++i) {
            store(i, bytes.data() + cursor + i * bytesPerPixel);
        }
        return image;
    }

    // Run-length encoded packets.
    usize written = 0;
    while (written < pixelCount) {
        if (cursor >= bytes.size()) {
            return fail("tga: truncated RLE data");
        }
        const u8 packet = bytes[cursor++];
        const usize count = static_cast<usize>(packet & 0x7f) + 1;
        if (written + count > pixelCount) {
            return fail("tga: RLE packet overruns the image");
        }
        if ((packet & 0x80) != 0) {
            if (cursor + bytesPerPixel > bytes.size()) {
                return fail("tga: truncated RLE data");
            }
            for (usize i = 0; i < count; ++i) {
                store(written + i, bytes.data() + cursor);
            }
            cursor += bytesPerPixel;
        } else {
            if (cursor + count * bytesPerPixel > bytes.size()) {
                return fail("tga: truncated RLE data");
            }
            for (usize i = 0; i < count; ++i) {
                store(written + i, bytes.data() + cursor + i * bytesPerPixel);
            }
            cursor += count * bytesPerPixel;
        }
        written += count;
    }
    return image;
}

// --- PNG -------------------------------------------------------------------------

namespace {

u32 crc32(const u8* data, usize size, u32 crc = 0) {
    static const auto table = [] {
        std::array<u32, 256> values{};
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            values[n] = c;
        }
        return values;
    }();

    crc = ~crc;
    for (usize i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

u32 adler32(const u8* data, usize size) {
    u32 a = 1;
    u32 b = 0;
    for (usize i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void appendBigEndian(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value >> 24));
    out.push_back(static_cast<u8>(value >> 16));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value));
}

void appendChunk(std::vector<u8>& out, const char type[4], const std::vector<u8>& payload) {
    appendBigEndian(out, static_cast<u32>(payload.size()));
    const usize crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    appendBigEndian(out, crc32(out.data() + crcStart, out.size() - crcStart));
}

}  // namespace

std::vector<u8> encodePng(const ImageData& image) {
    // Raw scanlines, each prefixed with filter type 0 (none).
    const usize rowBytes = static_cast<usize>(image.width) * 4;
    std::vector<u8> raw;
    raw.reserve((rowBytes + 1) * image.height);
    for (u32 y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const u8* row = image.pixels.data() + y * rowBytes;
        raw.insert(raw.end(), row, row + rowBytes);
    }

    // zlib stream made of "stored" deflate blocks (at most 65535 bytes each).
    std::vector<u8> zlib;
    zlib.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    usize offset = 0;
    do {
        const usize blockSize = std::min<usize>(raw.size() - offset, 65535);
        const bool last = offset + blockSize == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<u8>(blockSize & 0xff));
        zlib.push_back(static_cast<u8>(blockSize >> 8));
        zlib.push_back(static_cast<u8>(~blockSize & 0xff));
        zlib.push_back(static_cast<u8>((~blockSize >> 8) & 0xff));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
        offset += blockSize;
    } while (offset < raw.size());
    appendBigEndian(zlib, adler32(raw.data(), raw.size()));

    std::vector<u8> header;
    appendBigEndian(header, image.width);
    appendBigEndian(header, image.height);
    header.insert(header.end(), {8 /*bit depth*/, 6 /*RGBA*/, 0, 0, 0});

    std::vector<u8> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    appendChunk(png, "IHDR", header);
    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});
    return png;
}

Result<> writePng(const std::filesystem::path& path, const ImageData& image) {
    if (!image.valid()) {
        return fail("cannot write '{}': invalid image", path.string());
    }
    const std::vector<u8> png = encodePng(image);
    return fs::writeBinaryFile(path, png.data(), png.size());
}

}  // namespace bdex::assets
