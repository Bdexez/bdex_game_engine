#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bdex::fs {

[[nodiscard]] Result<std::vector<u8>> readBinaryFile(const std::filesystem::path& path);
[[nodiscard]] Result<std::string> readTextFile(const std::filesystem::path& path);
[[nodiscard]] Result<> writeBinaryFile(const std::filesystem::path& path, const void* data,
                                       usize size);

/// Directory containing the running executable (plugins and assets live
/// next to it).
[[nodiscard]] std::filesystem::path executableDirectory();
[[nodiscard]] std::filesystem::path executablePath();

}  // namespace bdex::fs

namespace bdex {

/// Maps logical roots to directories so that code never hard-codes disk
/// locations: "engine:/shaders/mesh.vert.spv", "app:/textures/crate.tga"...
/// Paths without a "root:/" prefix are used as-is.
class VirtualFileSystem {
public:
    void mount(std::string_view root, std::filesystem::path directory);
    [[nodiscard]] bool isMounted(std::string_view root) const;

    [[nodiscard]] Result<std::filesystem::path> resolve(std::string_view virtualPath) const;
    [[nodiscard]] Result<std::vector<u8>> readBinary(std::string_view virtualPath) const;
    [[nodiscard]] Result<std::string> readText(std::string_view virtualPath) const;

private:
    std::unordered_map<std::string, std::filesystem::path> mounts_;
};

}  // namespace bdex
