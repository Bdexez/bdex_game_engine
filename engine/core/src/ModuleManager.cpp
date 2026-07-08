#include <bdex/core/ModuleManager.hpp>

#include <bdex/core/Log.hpp>

#include <algorithm>
#include <map>
#include <system_error>
#include <unordered_map>

namespace bdex {
namespace {

std::map<std::string, ModuleFactory, std::less<>>& staticFactories() {
    static std::map<std::string, ModuleFactory, std::less<>> factories;
    return factories;
}

std::string join(const std::vector<std::string>& items) {
    std::string result;
    for (const std::string& item : items) {
        if (!result.empty()) {
            result += ", ";
        }
        result += item;
    }
    return result;
}

}  // namespace

// --- StaticModuleRegistry ----------------------------------------------------

void StaticModuleRegistry::add(std::string name, ModuleFactory factory) {
    staticFactories().insert_or_assign(std::move(name), factory);
}

ModuleFactory StaticModuleRegistry::find(std::string_view name) {
    const auto& factories = staticFactories();
    const auto it = factories.find(name);
    return it == factories.end() ? nullptr : it->second;
}

std::vector<std::string> StaticModuleRegistry::names() {
    std::vector<std::string> result;
    for (const auto& [name, factory] : staticFactories()) {
        result.push_back(name);
    }
    return result;
}

// --- ModuleManager -----------------------------------------------------------

ModuleManager::ModuleManager() = default;

ModuleManager::~ModuleManager() {
    // Normal shutdown goes through shutdownAll(); this only covers early exits.
    entries_.clear();
    unloadLibraries();
}

void ModuleManager::addSearchPath(std::filesystem::path path) {
    searchPaths_.push_back(std::move(path));
}

Result<> ModuleManager::load(std::string_view name) {
    if (isLoaded(name)) {
        return {};
    }

    // 1. Statically linked module.
    if (const ModuleFactory factory = StaticModuleRegistry::find(name)) {
        log::debug("modules", "loading '{}' (static)", name);
        return add(std::unique_ptr<IModule>(factory()));
    }

    // 2. Plugin next to the executable (or in any other search path).
    const std::string fileName = DynamicLibrary::fileName("bdex_module_" + std::string(name));
    std::string lastError = "not found in the static registry or in any search path";
    for (const std::filesystem::path& directory : searchPaths_) {
        const std::filesystem::path candidate = directory / fileName;
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec)) {
            continue;
        }

        auto library = DynamicLibrary::open(candidate);
        if (!library) {
            lastError = library.error().message;
            continue;
        }

        const std::string abiSymbol = "bdex_module_abi_" + std::string(name);
        const std::string createSymbol = "bdex_create_module_" + std::string(name);
        const auto abi = reinterpret_cast<u32 (*)()>(library->symbol(abiSymbol.c_str()));
        const auto create = reinterpret_cast<ModuleFactory>(library->symbol(createSymbol.c_str()));
        if (abi == nullptr || create == nullptr) {
            return fail("'{}' is not a bdex module (missing entry point '{}')",
                        candidate.string(), createSymbol);
        }
        if (abi() != kModuleAbiVersion) {
            return fail("module '{}' was built for ABI {} but the engine uses ABI {}", name, abi(),
                        kModuleAbiVersion);
        }

        log::debug("modules", "loading '{}' from {}", name, candidate.string());
        std::unique_ptr<IModule> module(create());
        // Keep the library before the module so a failed add() destroys the
        // module while its code is still mapped.
        libraries_.push_back(std::move(*library));
        return add(std::move(module));
    }

    return fail("cannot load module '{}': {}", name, lastError);
}

Result<> ModuleManager::add(std::unique_ptr<IModule> module) {
    if (module == nullptr) {
        return fail("cannot add a null module");
    }
    ModuleInfo info = module->info();
    if (info.name.empty()) {
        return fail("module has an empty name");
    }
    if (isLoaded(info.name)) {
        return fail("module '{}' is already loaded", info.name);
    }
    entries_.push_back(Entry{std::move(module), std::move(info), false});
    return {};
}

Result<std::vector<usize>> ModuleManager::resolveOrder(std::span<const ModuleInfo> infos) {
    // capability -> indices of the modules providing it
    std::unordered_map<std::string, std::vector<usize>> providers;
    for (usize i = 0; i < infos.size(); ++i) {
        providers[infos[i].name].push_back(i);
        for (const std::string& capability : infos[i].provides) {
            if (capability != infos[i].name) {
                providers[capability].push_back(i);
            }
        }
    }

    // dependents[p] = modules that must come after p
    std::vector<std::vector<usize>> dependents(infos.size());
    std::vector<usize> pending(infos.size(), 0);
    const auto addEdges = [&](usize module, const std::vector<usize>& before) {
        for (const usize provider : before) {
            if (provider != module) {
                dependents[provider].push_back(module);
                ++pending[module];
            }
        }
    };

    for (usize i = 0; i < infos.size(); ++i) {
        for (const std::string& capability : infos[i].dependencies) {
            const auto it = providers.find(capability);
            if (it == providers.end()) {
                return fail("module '{}' requires '{}', which no loaded module provides",
                            infos[i].name, capability);
            }
            addEdges(i, it->second);
        }
        for (const std::string& capability : infos[i].after) {
            if (const auto it = providers.find(capability); it != providers.end()) {
                addEdges(i, it->second);
            }
        }
    }

    // Kahn's algorithm; always picking the lowest ready index keeps the result
    // stable with respect to load order.
    std::vector<usize> order;
    order.reserve(infos.size());
    std::vector<bool> done(infos.size(), false);
    for (usize step = 0; step < infos.size(); ++step) {
        usize next = infos.size();
        for (usize i = 0; i < infos.size(); ++i) {
            if (!done[i] && pending[i] == 0) {
                next = i;
                break;
            }
        }
        if (next == infos.size()) {
            std::vector<std::string> cycle;
            for (usize i = 0; i < infos.size(); ++i) {
                if (!done[i]) {
                    cycle.push_back(infos[i].name);
                }
            }
            return fail("cyclic module dependencies between: {}", join(cycle));
        }
        done[next] = true;
        order.push_back(next);
        for (const usize dependent : dependents[next]) {
            --pending[dependent];
        }
    }
    return order;
}

Result<> ModuleManager::initializeAll(Engine& engine) {
    std::vector<ModuleInfo> infos;
    infos.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        infos.push_back(entry.info);
    }

    const auto order = resolveOrder(infos);
    if (!order) {
        return std::unexpected(order.error());
    }

    std::vector<usize> initializedNow;
    for (const usize index : *order) {
        Entry& entry = entries_[index];
        if (entry.initialized) {
            continue;
        }
        log::info("modules", "initializing '{}'", entry.info.name);
        if (const auto result = entry.module->onInit(engine); !result) {
            // Roll back what this call initialized so the engine is left in
            // the state it was in before.
            for (auto it = initializedNow.rbegin(); it != initializedNow.rend(); ++it) {
                Entry& done = entries_[*it];
                done.module->onShutdown(engine);
                done.initialized = false;
                std::erase(initOrder_, done.info.name);
            }
            return fail("module '{}' failed to initialize: {}", entry.info.name,
                        result.error().message);
        }
        entry.initialized = true;
        initializedNow.push_back(index);
        initOrder_.push_back(entry.info.name);
    }
    return {};
}

void ModuleManager::shutdownAll(Engine& engine) {
    for (auto it = initOrder_.rbegin(); it != initOrder_.rend(); ++it) {
        if (Entry* entry = findEntry(*it); entry != nullptr && entry->initialized) {
            log::info("modules", "shutting down '{}'", entry->info.name);
            entry->module->onShutdown(engine);
            entry->initialized = false;
        }
    }
    initOrder_.clear();
    // Destroy instances in reverse load order.
    while (!entries_.empty()) {
        entries_.pop_back();
    }
}

void ModuleManager::unloadLibraries() {
    while (!libraries_.empty()) {
        libraries_.pop_back();
    }
}

bool ModuleManager::isLoaded(std::string_view name) const {
    return findEntry(name) != nullptr;
}

bool ModuleManager::isInitialized(std::string_view name) const {
    const Entry* entry = findEntry(name);
    return entry != nullptr && entry->initialized;
}

std::vector<ModuleInfo> ModuleManager::modules() const {
    std::vector<ModuleInfo> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        result.push_back(entry.info);
    }
    return result;
}

ModuleManager::Entry* ModuleManager::findEntry(std::string_view name) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [name](const Entry& entry) { return entry.info.name == name; });
    return it == entries_.end() ? nullptr : &*it;
}

const ModuleManager::Entry* ModuleManager::findEntry(std::string_view name) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [name](const Entry& entry) { return entry.info.name == name; });
    return it == entries_.end() ? nullptr : &*it;
}

}  // namespace bdex
