#pragma once

#include <bdex/core/FileSystem.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/core/TypeId.hpp>

#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bdex::assets {

/// SPIR-V module as loaded from disk.
struct ShaderBinary {
    std::vector<u32> words;
};

/// Loads and caches assets by virtual path.
///
/// Loaders are registered per (asset type, file extension), so support for a
/// new format can be added from anywhere - an application or a module - without
/// modifying this class:
///
///   assets.registerLoader<ImageData>(".png", [](std::span<const u8> bytes) { ... });
///   auto image = assets.load<ImageData>("app:/textures/crate.png");
///
/// Built-in loaders: MeshData (.obj), ImageData (.tga), ShaderBinary (.spv).
class AssetManager {
public:
    explicit AssetManager(const VirtualFileSystem& vfs);

    template <class T>
    using Loader = std::function<Result<T>(std::span<const u8>)>;

    /// `extension` includes the dot and is matched case-sensitively (".obj").
    template <class T>
    void registerLoader(std::string_view extension, Loader<T> loader) {
        loaders_[{typeId<T>(), std::string(extension)}] =
            [loader = std::move(loader)](std::span<const u8> bytes) -> Result<std::shared_ptr<void>> {
            Result<T> result = loader(bytes);
            if (!result) {
                return std::unexpected(result.error());
            }
            return std::make_shared<T>(std::move(*result));
        };
    }

    /// Returns the cached asset, loading it on first use.
    template <class T>
    [[nodiscard]] Result<std::shared_ptr<const T>> load(std::string_view virtualPath) {
        auto result = loadErased(typeId<T>(), typeName<T>(), virtualPath);
        if (!result) {
            return std::unexpected(result.error());
        }
        return std::static_pointer_cast<const T>(*result);
    }

    /// Drops the cache; assets still referenced elsewhere stay alive.
    void clearCache() { cache_.clear(); }
    [[nodiscard]] usize cachedCount() const { return cache_.size(); }

private:
    using ErasedLoader = std::function<Result<std::shared_ptr<void>>(std::span<const u8>)>;
    using Key = std::pair<TypeId, std::string>;

    Result<std::shared_ptr<void>> loadErased(TypeId type, std::string_view typeName,
                                             std::string_view virtualPath);

    const VirtualFileSystem* vfs_;
    std::map<Key, ErasedLoader> loaders_;  // (type, extension)
    std::map<Key, std::shared_ptr<void>> cache_;  // (type, virtual path)
};

}  // namespace bdex::assets
