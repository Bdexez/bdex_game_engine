#include <bdex/rhi/Types.hpp>

namespace bdex::rhi {

bool isDepthFormat(Format format) {
    return format == Format::D32Float || format == Format::D24UnormS8Uint;
}

bool hasStencil(Format format) {
    return format == Format::D24UnormS8Uint;
}

u32 bytesPerPixel(Format format) {
    switch (format) {
        case Format::RGBA8Unorm:
        case Format::RGBA8Srgb:
        case Format::BGRA8Unorm:
        case Format::BGRA8Srgb:
        case Format::D32Float:
        case Format::D24UnormS8Uint: return 4;
        case Format::RGBA16Float: return 8;
        case Format::Undefined: break;
    }
    return 0;
}

std::string_view formatName(Format format) {
    switch (format) {
        case Format::Undefined: return "Undefined";
        case Format::RGBA8Unorm: return "RGBA8Unorm";
        case Format::RGBA8Srgb: return "RGBA8Srgb";
        case Format::BGRA8Unorm: return "BGRA8Unorm";
        case Format::BGRA8Srgb: return "BGRA8Srgb";
        case Format::RGBA16Float: return "RGBA16Float";
        case Format::D32Float: return "D32Float";
        case Format::D24UnormS8Uint: return "D24UnormS8Uint";
    }
    return "?";
}

}  // namespace bdex::rhi
