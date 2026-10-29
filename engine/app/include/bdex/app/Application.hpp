#pragma once

#include <bdex/assets/AssetManager.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/platform/Input.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/scene/Scene.hpp>

#include <memory>
#include <string>

namespace bdex::app {

/// Base class for games and tools. It is a thin composition layer on top of
/// the Engine kernel: it builds the configuration, registers the common
/// services, loads the modules listed in `engine.modules` and runs the loop.
///
/// Nothing here is mandatory - an application can also drive bdex::Engine
/// directly - but this is the shortest path to a running game:
///
///   class MyGame : public bdex::app::Application {
///       bdex::Result<> onStart() override { ...build the scene...; return {}; }
///       void onUpdate(const bdex::FrameTime& time) override { ... }
///   };
///   BDEX_MAIN(MyGame)
///
/// Configuration precedence (lowest to highest): built-in defaults,
/// onConfigure(), the config file, the command line.
///
/// Command line:
///   --config <file>      INI file (default: <executable>.ini if present)
///   --set <key>=<value>  override any configuration key
///   --modules <a,b,c>    modules to load (engine.modules)
///   --headless           swap the window module for window_null
///   --frames <n>         exit after n frames (engine.max_frames)
///   --screenshot <file>  save a PNG of the last frame (or of frame
///                        app.screenshot_frame)
///   --log-level <level>  trace | debug | info | warn | error | off
///   --strict             exit with code 3 if the RHI reported validation errors
///   --help
///
/// Exit codes: 0 success, 1 startup failure, 2 bad command line,
/// 3 validation errors in --strict mode.
class Application {
public:
    Application();
    virtual ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(int argc, char** argv);

protected:
    /// Sets application defaults (window title, module list...).
    virtual void onConfigure(Config& /*config*/) {}
    /// Called once every module is initialized. Build the scene here.
    virtual Result<> onStart() { return {}; }
    virtual void onUpdate(const FrameTime& /*time*/) {}
    virtual void onFixedUpdate(const FrameTime& /*time*/) {}
    /// Called after the last frame, while every module is still alive.
    virtual void onStop() {}

    [[nodiscard]] Engine& engine();
    /// The active scene (a default one is created at startup).
    [[nodiscard]] scene::Scene& scene();
    [[nodiscard]] assets::AssetManager& assets();
    /// Aborts with a clear message if no renderer module is loaded.
    [[nodiscard]] render::IRenderer& renderer();
    /// Null when no window module is loaded.
    [[nodiscard]] platform::InputState* input();
    [[nodiscard]] platform::IWindow* window();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace bdex::app

/// Defines main() for an Application subclass.
#define BDEX_MAIN(ApplicationClass)       \
    int main(int argc, char** argv) {     \
        ApplicationClass application;     \
        return application.run(argc, argv); \
    }
