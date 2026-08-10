// window_glfw: desktop windows and input through GLFW.
//
// Configuration:
//   window.title                   (default "bdex")
//   window.width, window.height    (default 1280x720)
//   window.resizable               (default true)

// GLFW only needs the Vulkan *types* to declare glfwCreateWindowSurface; no
// Vulkan function is called from this module.
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <bdex/core/Engine.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/core/Module.hpp>
#include <bdex/platform/Events.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>

#include <algorithm>
#include <memory>

namespace bdex {
namespace {

using namespace platform;

Key translateKey(int key) {
    // Printable keys and the engine's Key enum both follow the US-layout
    // ASCII codes, and the function/navigation keys share GLFW's numbering,
    // so the supported ranges map one to one.
    const bool printable = key >= GLFW_KEY_SPACE && key <= GLFW_KEY_GRAVE_ACCENT;
    const bool navigation = key >= GLFW_KEY_ESCAPE && key <= GLFW_KEY_END;
    const bool function = key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12;
    const bool modifier = key >= GLFW_KEY_LEFT_SHIFT && key <= GLFW_KEY_RIGHT_SUPER;
    if (printable || navigation || function || modifier || key == GLFW_KEY_CAPS_LOCK) {
        return static_cast<Key>(key);
    }
    return Key::Unknown;
}

class GlfwWindow final : public IWindow {
public:
    GlfwWindow(GLFWwindow* handle, EventBus& events) : handle_(handle), events_(&events) {
        glfwSetWindowUserPointer(handle_, this);

        glfwSetWindowCloseCallback(handle_, [](GLFWwindow* window) {
            GlfwWindow& self = from(window);
            self.events_->publish(WindowCloseRequested{&self});
        });
        glfwSetFramebufferSizeCallback(handle_, [](GLFWwindow* window, int width, int height) {
            GlfwWindow& self = from(window);
            self.events_->publish(WindowResized{
                &self, {static_cast<u32>(std::max(width, 0)), static_cast<u32>(std::max(height, 0))}});
        });
        glfwSetWindowFocusCallback(handle_, [](GLFWwindow* window, int focused) {
            GlfwWindow& self = from(window);
            self.events_->publish(WindowFocusChanged{&self, focused == GLFW_TRUE});
        });
        glfwSetKeyCallback(handle_, [](GLFWwindow* window, int key, int, int action, int) {
            const Key translated = translateKey(key);
            if (translated == Key::Unknown) {
                return;
            }
            const KeyAction keyAction = action == GLFW_PRESS     ? KeyAction::Press
                                        : action == GLFW_RELEASE ? KeyAction::Release
                                                                 : KeyAction::Repeat;
            from(window).events_->publish(KeyEvent{translated, keyAction});
        });
        glfwSetMouseButtonCallback(handle_, [](GLFWwindow* window, int button, int action, int) {
            if (button < 0 || button >= static_cast<int>(MouseButton::Count)) {
                return;
            }
            from(window).events_->publish(
                MouseButtonEvent{static_cast<MouseButton>(button), action == GLFW_PRESS});
        });
        glfwSetCursorPosCallback(handle_, [](GLFWwindow* window, double x, double y) {
            from(window).events_->publish(MouseMoved{x, y});
        });
        glfwSetScrollCallback(handle_, [](GLFWwindow* window, double dx, double dy) {
            from(window).events_->publish(MouseScrolled{dx, dy});
        });
    }

    ~GlfwWindow() override { glfwDestroyWindow(handle_); }

    [[nodiscard]] Extent2D framebufferSize() const override {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(handle_, &width, &height);
        return {static_cast<u32>(std::max(width, 0)), static_cast<u32>(std::max(height, 0))};
    }

    [[nodiscard]] bool shouldClose() const override {
        return glfwWindowShouldClose(handle_) == GLFW_TRUE;
    }

    void requestClose() override {
        glfwSetWindowShouldClose(handle_, GLFW_TRUE);
        events_->enqueue(WindowCloseRequested{this});
    }

    void setTitle(std::string_view title) override {
        glfwSetWindowTitle(handle_, std::string(title).c_str());
    }

    void setCursorCaptured(bool captured) override {
        glfwSetInputMode(handle_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    [[nodiscard]] Result<u64> createVulkanSurface(void* vkInstance) override {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        const VkResult result =
            glfwCreateWindowSurface(static_cast<VkInstance>(vkInstance), handle_, nullptr, &surface);
        if (result != VK_SUCCESS) {
            return fail("glfwCreateWindowSurface failed (VkResult {})", static_cast<int>(result));
        }
        // VkSurfaceKHR is a pointer or a 64-bit integer depending on the
        // platform; a C-style cast covers both definitions.
        return (u64)surface;  // NOLINT(google-readability-casting)
    }

private:
    static GlfwWindow& from(GLFWwindow* window) {
        return *static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
    }

    GLFWwindow* handle_;
    EventBus* events_;
};

class GlfwWindowSystem final : public IWindowSystem {
public:
    explicit GlfwWindowSystem(std::unique_ptr<GlfwWindow> window) : window_(std::move(window)) {}

    [[nodiscard]] IWindow* primaryWindow() override { return window_.get(); }
    [[nodiscard]] bool isHeadless() const override { return false; }

    [[nodiscard]] std::vector<std::string> vulkanInstanceExtensions() const override {
        u32 count = 0;
        const char** names = glfwGetRequiredInstanceExtensions(&count);
        std::vector<std::string> extensions;
        for (u32 i = 0; names != nullptr && i < count; ++i) {
            extensions.emplace_back(names[i]);
        }
        return extensions;
    }

    void pollEvents() override { glfwPollEvents(); }

private:
    std::unique_ptr<GlfwWindow> window_;
};

class GlfwWindowModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "GLFW windows and input",
            .provides = {"window"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        glfwSetErrorCallback([](int code, const char* description) {
            log::error("glfw", "error {}: {}", code, description);
        });
        if (glfwInit() != GLFW_TRUE) {
            return fail("glfwInit failed (is a display server available? try --headless)");
        }
        glfwInitialized_ = true;

        const Config& config = engine.config();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // the RHI owns the graphics API
        glfwWindowHint(GLFW_RESIZABLE, config.getBool("window.resizable", true) ? GLFW_TRUE : GLFW_FALSE);

        const int width = static_cast<int>(std::clamp<i64>(config.getInt("window.width", 1280), 1, 16384));
        const int height = static_cast<int>(std::clamp<i64>(config.getInt("window.height", 720), 1, 16384));
        GLFWwindow* handle = glfwCreateWindow(
            width, height, config.getString("window.title", "bdex").c_str(), nullptr, nullptr);
        if (handle == nullptr) {
            glfwTerminate();
            glfwInitialized_ = false;
            return fail("glfwCreateWindow failed");
        }

        system_ = std::make_shared<GlfwWindowSystem>(
            std::make_unique<GlfwWindow>(handle, engine.events()));
        engine.services().provide<IWindowSystem>(system_);

        input_ = std::make_shared<InputState>(engine.events());
        engine.services().provide<InputState>(input_);

        // Reset the per-frame input transitions, then let GLFW publish this
        // frame's events.
        pollTask_ = ScopedTask(
            engine.scheduler(),
            engine.scheduler().add(Phase::Input, "window_glfw.poll_events",
                                   [input = input_.get(), system = system_.get()](const FrameTime&) {
                                       input->beginFrame();
                                       system->pollEvents();
                                   }));
        return {};
    }

    void onShutdown(Engine& engine) override {
        pollTask_.reset();
        engine.services().remove<InputState>();
        engine.services().remove<IWindowSystem>();
        input_.reset();
        system_.reset();  // destroys the window
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
    }

private:
    std::shared_ptr<GlfwWindowSystem> system_;
    std::shared_ptr<InputState> input_;
    ScopedTask pollTask_;
    bool glfwInitialized_ = false;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::GlfwWindowModule)
