// window_null: a window system without any display.
//
// It lets the whole engine - including a real GPU backend rendering
// offscreen - run on machines with no display server (CI, servers, automated
// captures) without the rest of the code noticing.
//
// Configuration:
//   window.width, window.height   size reported to renderers (default 1280x720)

#include <bdex/core/Engine.hpp>
#include <bdex/core/Module.hpp>
#include <bdex/platform/Events.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>

#include <algorithm>
#include <memory>

namespace bdex {
namespace {

class NullWindow final : public platform::IWindow {
public:
    NullWindow(EventBus& events, Extent2D size) : events_(&events), size_(size) {}

    [[nodiscard]] Extent2D framebufferSize() const override { return size_; }
    [[nodiscard]] bool shouldClose() const override { return closeRequested_; }

    void requestClose() override {
        if (!closeRequested_) {
            closeRequested_ = true;
            events_->enqueue(platform::WindowCloseRequested{this});
        }
    }

    void setTitle(std::string_view) override {}
    void setCursorCaptured(bool) override {}

    [[nodiscard]] Result<u64> createVulkanSurface(void*) override {
        return fail("window_null has no presentation surface");
    }

private:
    EventBus* events_;
    Extent2D size_;
    bool closeRequested_ = false;
};

class NullWindowSystem final : public platform::IWindowSystem {
public:
    NullWindowSystem(EventBus& events, Extent2D size) : window_(events, size) {}

    [[nodiscard]] platform::IWindow* primaryWindow() override { return &window_; }
    [[nodiscard]] bool isHeadless() const override { return true; }
    [[nodiscard]] std::vector<std::string> vulkanInstanceExtensions() const override { return {}; }
    void pollEvents() override {}

private:
    NullWindow window_;
};

class NullWindowModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "Headless window system",
            .provides = {"window"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        const Config& config = engine.config();
        const Extent2D size{
            static_cast<u32>(std::clamp<i64>(config.getInt("window.width", 1280), 1, 16384)),
            static_cast<u32>(std::clamp<i64>(config.getInt("window.height", 720), 1, 16384)),
        };

        engine.services().provide<platform::IWindowSystem>(
            std::make_shared<NullWindowSystem>(engine.events(), size));

        // No device produces input, but the service exists so gameplay code
        // (and tests injecting input events) behaves the same as with a
        // real window.
        input_ = std::make_shared<platform::InputState>(engine.events());
        engine.services().provide<platform::InputState>(input_);
        inputTask_ = ScopedTask(engine.scheduler(),
                                engine.scheduler().add(
                                    Phase::Input, "window_null.input",
                                    [input = input_.get()](const FrameTime&) { input->beginFrame(); }));
        return {};
    }

    void onShutdown(Engine& engine) override {
        inputTask_.reset();
        engine.services().remove<platform::InputState>();
        engine.services().remove<platform::IWindowSystem>();
        input_.reset();
    }

private:
    std::shared_ptr<platform::InputState> input_;
    ScopedTask inputTask_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::NullWindowModule)
