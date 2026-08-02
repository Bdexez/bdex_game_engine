#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/core/Types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace bdex::platform {

/// A native window (or a stand-in for one when running headless).
class IWindow {
public:
    virtual ~IWindow() = default;

    /// Size of the drawable area in pixels. {0, 0} while minimized.
    [[nodiscard]] virtual Extent2D framebufferSize() const = 0;
    [[nodiscard]] virtual bool shouldClose() const = 0;
    virtual void requestClose() = 0;
    virtual void setTitle(std::string_view title) = 0;
    /// Hides the cursor and delivers unbounded relative motion (camera look).
    virtual void setCursorCaptured(bool captured) = 0;

    /// Creates a Vulkan presentation surface for this window.
    ///
    /// The window backend is the only component that knows the native
    /// windowing API (X11, Wayland, Win32...), so it is the one creating the
    /// surface; the Vulkan types are passed opaquely to keep this interface
    /// free of any graphics API header:
    ///   - `vkInstance` is a `VkInstance`,
    ///   - the returned value is a `VkSurfaceKHR`, owned by the caller.
    /// Fails for windows that cannot be presented to (headless).
    [[nodiscard]] virtual Result<u64> createVulkanSurface(void* vkInstance) = 0;
};

/// Service provided by a window module ("window" capability).
class IWindowSystem {
public:
    virtual ~IWindowSystem() = default;

    [[nodiscard]] virtual IWindow* primaryWindow() = 0;

    /// True when there is no real display: renderers should draw offscreen.
    [[nodiscard]] virtual bool isHeadless() const = 0;

    /// Vulkan instance extensions required to create surfaces for this
    /// window system. Empty when headless.
    [[nodiscard]] virtual std::vector<std::string> vulkanInstanceExtensions() const = 0;

    /// Pumps OS events and publishes them on the engine EventBus.
    virtual void pollEvents() = 0;
};

}  // namespace bdex::platform
