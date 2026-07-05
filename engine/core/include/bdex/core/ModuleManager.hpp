#pragma once

#include <bdex/core/DynamicLibrary.hpp>
#include <bdex/core/Module.hpp>
#include <bdex/core/Result.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bdex {

/// Process-wide table of modules linked statically into the executable.
/// Filled before main() by the translation unit that bdex_target_modules()
/// generates; empty when modules are built as plugins.
class StaticModuleRegistry {
public:
    static void add(std::string name, ModuleFactory factory);
    [[nodiscard]] static ModuleFactory find(std::string_view name);
    [[nodiscard]] static std::vector<std::string> names();
};

/// Loads modules, orders them by their declared dependencies and drives their
/// lifecycle.
///
/// `load(name)` is agnostic to how the module was built: it first looks in
/// the StaticModuleRegistry, then for a plugin `bdex_module_<name>` in the
/// search paths. Application code is identical in both configurations.
class ModuleManager {
public:
    ModuleManager();
    ~ModuleManager();
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    /// Directory searched for plugins (the executable directory is the usual
    /// choice). Paths are tried in insertion order.
    void addSearchPath(std::filesystem::path path);

    /// Loads a module by name. Loading the same module twice is a no-op.
    Result<> load(std::string_view name);

    /// Registers an already constructed module (application-defined modules,
    /// tests).
    Result<> add(std::unique_ptr<IModule> module);

    /// Initializes every loaded, not yet initialized module in dependency
    /// order. On failure, modules initialized by this call are shut down
    /// again and the error is returned.
    Result<> initializeAll(Engine& engine);

    /// Shuts initialized modules down in reverse initialization order and
    /// destroys every module instance. Plugin libraries stay mapped until
    /// unloadLibraries() so that code they contributed (callbacks, vtables)
    /// can still be destroyed safely by the caller.
    void shutdownAll(Engine& engine);

    /// Unmaps plugin libraries. Call once nothing created by a plugin is alive.
    void unloadLibraries();

    [[nodiscard]] bool isLoaded(std::string_view name) const;
    [[nodiscard]] bool isInitialized(std::string_view name) const;
    /// Infos of the loaded modules, in load order.
    [[nodiscard]] std::vector<ModuleInfo> modules() const;
    /// Names of the initialized modules, in initialization order.
    [[nodiscard]] const std::vector<std::string>& initializationOrder() const {
        return initOrder_;
    }

    /// Computes an initialization order (indices into `infos`) satisfying
    /// `dependencies` and `after`. Fails on a missing capability or a cycle.
    /// Ties are resolved by position, which keeps the order deterministic.
    [[nodiscard]] static Result<std::vector<usize>> resolveOrder(std::span<const ModuleInfo> infos);

private:
    struct Entry {
        std::unique_ptr<IModule> module;
        ModuleInfo info;
        bool initialized = false;
    };

    [[nodiscard]] Entry* findEntry(std::string_view name);
    [[nodiscard]] const Entry* findEntry(std::string_view name) const;

    // Declaration order matters: libraries_ must outlive entries_ because the
    // module objects' code lives in the libraries.
    std::vector<DynamicLibrary> libraries_;
    std::vector<Entry> entries_;
    std::vector<std::string> initOrder_;
    std::vector<std::filesystem::path> searchPaths_;
};

}  // namespace bdex
