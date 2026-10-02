// rhi_vulkan: Vulkan 1.3 implementation of the render hardware interface.
//
// Configuration:
//   rhi.validation   enable the Khronos validation layer
//                    (default: true in debug builds, false otherwise)
//   rhi.adapter      substring of the GPU name to prefer (default: best GPU)

#include "VulkanDevice.hpp"

#include <bdex/core/Engine.hpp>
#include <bdex/core/Module.hpp>
#include <bdex/platform/Window.hpp>

namespace bdex {
namespace {

class VulkanRhiModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "Vulkan 1.3 render hardware interface",
            .provides = {"rhi"},
            .dependencies = {},
            // Works without a window system (offscreen), but when there is
            // one it must come first: it dictates the instance extensions.
            .after = {"window"},
        };
    }

    Result<> onInit(Engine& engine) override {
#if defined(NDEBUG)
        constexpr bool kValidationDefault = false;
#else
        constexpr bool kValidationDefault = true;
#endif
        const Config& config = engine.config();

        vulkan::DeviceConfig deviceConfig;
        deviceConfig.applicationName = config.getString("app.name", "bdex");
        deviceConfig.validation = config.getBool("rhi.validation", kValidationDefault);
        deviceConfig.preferredAdapter = config.getString("rhi.adapter");

        // The only thing this backend needs to know about windowing: which
        // instance extensions the window system wants. Surfaces themselves
        // are created by the windows (IWindow::createVulkanSurface).
        if (const auto* windows = engine.services().tryGet<platform::IWindowSystem>();
            windows != nullptr && !windows->isHeadless()) {
            deviceConfig.instanceExtensions = windows->vulkanInstanceExtensions();
            deviceConfig.presentation = true;
        }

        auto device = vulkan::Device::create(deviceConfig);
        if (!device) {
            return std::unexpected(device.error());
        }
        device_ = std::move(*device);
        engine.services().provide<rhi::IDevice>(device_);
        return {};
    }

    void onShutdown(Engine& engine) override {
        engine.services().remove<rhi::IDevice>();
        device_.reset();
    }

private:
    std::shared_ptr<vulkan::Device> device_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::VulkanRhiModule)
