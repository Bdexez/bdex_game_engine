#include <bdex/assets/AssetManager.hpp>

#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/Log.hpp>

#include <cstring>
#include <filesystem>

namespace bdex::assets {

AssetManager::AssetManager(const VirtualFileSystem& vfs) : vfs_(&vfs) {
    registerLoader<MeshData>(".obj", [](std::span<const u8> bytes) {
        return parseObj(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    });

    registerLoader<ImageData>(".tga", [](std::span<const u8> bytes) { return decodeTga(bytes); });

    registerLoader<ShaderBinary>(".spv", [](std::span<const u8> bytes) -> Result<ShaderBinary> {
        constexpr u32 kSpirvMagic = 0x07230203;
        if (bytes.size() < 4 || bytes.size() % 4 != 0) {
            return fail("spv: size is not a multiple of 4 bytes");
        }
        ShaderBinary binary;
        binary.words.resize(bytes.size() / 4);
        std::memcpy(binary.words.data(), bytes.data(), bytes.size());
        if (binary.words.front() != kSpirvMagic) {
            return fail("spv: bad magic number");
        }
        return binary;
    });
}

Result<std::shared_ptr<void>> AssetManager::loadErased(TypeId type, std::string_view typeName,
                                                       std::string_view virtualPath) {
    const Key cacheKey{type, std::string(virtualPath)};
    if (const auto cached = cache_.find(cacheKey); cached != cache_.end()) {
        return cached->second;
    }

    const std::string extension = std::filesystem::path(virtualPath).extension().string();
    const auto loader = loaders_.find(Key{type, extension});
    if (loader == loaders_.end()) {
        return fail("no loader registered for '{}' files as {}", extension, typeName);
    }

    const auto bytes = vfs_->readBinary(virtualPath);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    auto asset = loader->second(*bytes);
    if (!asset) {
        return fail("failed to load '{}': {}", virtualPath, asset.error().message);
    }
    log::debug("assets", "loaded '{}'", virtualPath);
    cache_.emplace(cacheKey, *asset);
    return asset;
}

}  // namespace bdex::assets
