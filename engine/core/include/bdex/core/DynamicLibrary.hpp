#pragma once

#include <bdex/core/Result.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace bdex {

/// Move-only owner of a shared library loaded at runtime.
class DynamicLibrary {
public:
    DynamicLibrary() = default;
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    ~DynamicLibrary();

    [[nodiscard]] static Result<DynamicLibrary> open(const std::filesystem::path& path);

    /// Platform file name for a library: "foo" -> "libfoo.so" / "foo.dll" /
    /// "libfoo.dylib".
    [[nodiscard]] static std::string fileName(std::string_view baseName);

    [[nodiscard]] void* symbol(const char* name) const;
    [[nodiscard]] bool loaded() const { return handle_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    void close();

private:
    void* handle_ = nullptr;
    std::filesystem::path path_;
};

}  // namespace bdex
