#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <string>
#include <vector>

namespace bdex {

class Engine;

/// Bumped whenever the IModule contract or the core ABI changes. A plugin
/// built against another value is rejected at load time instead of crashing.
inline constexpr u32 kModuleAbiVersion = 1;

/// Static description of a module, used to compute the initialization order.
///
/// Dependencies are expressed on *capabilities*, not on module names: the
/// renderer requires "rhi", which can be satisfied by `rhi_vulkan`, `rhi_null`
/// or any future backend. A module always provides its own name.
struct ModuleInfo {
    std::string name;
    std::string description;
    /// Capabilities this module makes available (e.g. "window", "rhi").
    std::vector<std::string> provides;
    /// Capabilities that must be provided by another loaded module.
    std::vector<std::string> dependencies;
    /// Capabilities this module must be initialized after *if* they are
    /// present, without requiring them (e.g. the RHI wants to come after the
    /// window system, but also works headless).
    std::vector<std::string> after;
};

/// A unit of engine functionality with an explicit lifecycle.
///
/// A module talks to the rest of the engine exclusively through the Engine
/// facade: it provides and consumes services, publishes and subscribes to
/// events, and schedules tasks. It never links against another module.
class IModule {
public:
    virtual ~IModule() = default;

    [[nodiscard]] virtual ModuleInfo info() const = 0;

    /// Called once, after every dependency has been initialized.
    virtual Result<> onInit(Engine& engine) = 0;

    /// Called once, in reverse initialization order. Must release everything
    /// registered in onInit (services, tasks, subscriptions).
    virtual void onShutdown(Engine& engine) = 0;
};

using ModuleFactory = IModule* (*)();

}  // namespace bdex

// --- Module entry point ------------------------------------------------------

#if defined(_WIN32)
#define BDEX_MODULE_EXPORT __declspec(dllexport)
#else
#define BDEX_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#define BDEX_DETAIL_CONCAT_IMPL(a, b) a##b
#define BDEX_DETAIL_CONCAT(a, b) BDEX_DETAIL_CONCAT_IMPL(a, b)
#define BDEX_DETAIL_STRINGIFY_IMPL(x) #x
#define BDEX_DETAIL_STRINGIFY(x) BDEX_DETAIL_STRINGIFY_IMPL(x)

/// Name of the module being compiled, as a string literal. BDEX_MODULE_NAME is
/// defined by bdex_add_module() in CMake.
#define BDEX_MODULE_NAME_STRING BDEX_DETAIL_STRINGIFY(BDEX_MODULE_NAME)

/// Emits the entry points of a module. Use once, at global scope, in exactly
/// one source file of the module:
///
///   BDEX_DEFINE_MODULE(MyModule)
///
/// The exported symbols are `bdex_create_module_<name>` and
/// `bdex_module_abi_<name>`; the same symbols serve both static registration
/// and runtime plugin loading.
#define BDEX_DEFINE_MODULE(ModuleClass)                                              \
    extern "C" BDEX_MODULE_EXPORT ::bdex::IModule* BDEX_DETAIL_CONCAT(               \
        bdex_create_module_, BDEX_MODULE_NAME)() {                                   \
        return new ModuleClass();                                                    \
    }                                                                                \
    extern "C" BDEX_MODULE_EXPORT ::bdex::u32 BDEX_DETAIL_CONCAT(bdex_module_abi_,   \
                                                                BDEX_MODULE_NAME)() { \
        return ::bdex::kModuleAbiVersion;                                            \
    }
