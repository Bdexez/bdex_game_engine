#include <bdex/core/DynamicLibrary.hpp>

#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace bdex {

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), path_(std::move(other.path_)) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() {
    close();
}

Result<DynamicLibrary> DynamicLibrary::open(const std::filesystem::path& path) {
    DynamicLibrary library;
#if defined(_WIN32)
    library.handle_ = static_cast<void*>(LoadLibraryW(path.c_str()));
    if (library.handle_ == nullptr) {
        return fail("cannot load '{}' (error {})", path.string(), GetLastError());
    }
#else
    // RTLD_LOCAL: plugins only communicate through engine interfaces, their
    // symbols must not leak into (or clash with) other plugins.
    library.handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library.handle_ == nullptr) {
        const char* reason = dlerror();
        return fail("cannot load '{}': {}", path.string(), reason != nullptr ? reason : "unknown");
    }
#endif
    library.path_ = path;
    return library;
}

std::string DynamicLibrary::fileName(std::string_view baseName) {
#if defined(_WIN32)
    return std::string(baseName) + ".dll";
#elif defined(__APPLE__)
    return "lib" + std::string(baseName) + ".dylib";
#else
    return "lib" + std::string(baseName) + ".so";
#endif
}

void* DynamicLibrary::symbol(const char* name) const {
    if (handle_ == nullptr) {
        return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

void DynamicLibrary::close() {
    if (handle_ == nullptr) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
}

}  // namespace bdex
