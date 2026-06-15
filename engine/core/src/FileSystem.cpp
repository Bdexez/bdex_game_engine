#include <bdex/core/FileSystem.hpp>

#include <fstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace bdex::fs {

Result<std::vector<u8>> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return fail("cannot open '{}'", path.string());
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        return fail("cannot read '{}'", path.string());
    }
    std::vector<u8> data(static_cast<usize>(size));
    file.seekg(0);
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        return fail("cannot read '{}'", path.string());
    }
    return data;
}

Result<std::string> readTextFile(const std::filesystem::path& path) {
    auto bytes = readBinaryFile(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return std::string(bytes->begin(), bytes->end());
}

Result<> writeBinaryFile(const std::filesystem::path& path, const void* data, usize size) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail("cannot create '{}'", path.string());
    }
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file) {
        return fail("cannot write '{}'", path.string());
    }
    return {};
}

std::filesystem::path executablePath() {
    std::error_code ec;
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(buffer);
    }
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        auto path = std::filesystem::canonical(buffer, ec);
        if (!ec) {
            return path;
        }
    }
#else
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        return path;
    }
#endif
    return std::filesystem::current_path(ec) / "unknown";
}

std::filesystem::path executableDirectory() {
    return executablePath().parent_path();
}

}  // namespace bdex::fs

namespace bdex {

void VirtualFileSystem::mount(std::string_view root, std::filesystem::path directory) {
    mounts_.insert_or_assign(std::string(root), std::move(directory));
}

bool VirtualFileSystem::isMounted(std::string_view root) const {
    return mounts_.contains(std::string(root));
}

Result<std::filesystem::path> VirtualFileSystem::resolve(std::string_view virtualPath) const {
    const usize separator = virtualPath.find(":/");
    if (separator == std::string_view::npos) {
        return std::filesystem::path(virtualPath);
    }
    const std::string root(virtualPath.substr(0, separator));
    const auto it = mounts_.find(root);
    if (it == mounts_.end()) {
        return fail("unknown mount point '{}' in '{}'", root, virtualPath);
    }
    return it->second / std::filesystem::path(virtualPath.substr(separator + 2));
}

Result<std::vector<u8>> VirtualFileSystem::readBinary(std::string_view virtualPath) const {
    const auto path = resolve(virtualPath);
    if (!path) {
        return std::unexpected(path.error());
    }
    return fs::readBinaryFile(*path);
}

Result<std::string> VirtualFileSystem::readText(std::string_view virtualPath) const {
    const auto path = resolve(virtualPath);
    if (!path) {
        return std::unexpected(path.error());
    }
    return fs::readTextFile(*path);
}

}  // namespace bdex
