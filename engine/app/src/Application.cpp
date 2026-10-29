#include <bdex/app/Application.hpp>

#include <bdex/core/FileSystem.hpp>
#include <bdex/core/JobSystem.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/platform/Events.hpp>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace bdex::app {
namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitStartupFailure = 1;
constexpr int kExitBadCommandLine = 2;
constexpr int kExitValidationErrors = 3;

constexpr std::string_view kUsage =
    "Options:\n"
    "  --config <file>      INI configuration file\n"
    "  --set <key>=<value>  override a configuration key (repeatable)\n"
    "  --modules <a,b,c>    modules to load\n"
    "  --headless           run without a window (offscreen rendering)\n"
    "  --frames <n>         exit after n frames\n"
    "  --screenshot <file>  save a PNG capture\n"
    "  --log-level <level>  trace | debug | info | warn | error | off\n"
    "  --strict             fail if the RHI reports validation errors\n"
    "  --help               show this message\n";

struct CommandLine {
    std::optional<std::string> configFile;
    Config overrides;
    bool help = false;
};

Result<CommandLine> parseCommandLine(int argc, char** argv) {
    CommandLine cli;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> Result<std::string_view> {
            if (i + 1 >= argc) {
                return fail("option '{}' expects a value", arg);
            }
            return std::string_view(argv[++i]);
        };

        if (arg == "--help" || arg == "-h") {
            cli.help = true;
        } else if (arg == "--headless") {
            cli.overrides.set("app.headless", "true");
        } else if (arg == "--strict") {
            cli.overrides.set("app.strict", "true");
        } else if (arg == "--config") {
            const auto v = value();
            if (!v) return std::unexpected(v.error());
            cli.configFile = std::string(*v);
        } else if (arg == "--modules") {
            const auto v = value();
            if (!v) return std::unexpected(v.error());
            cli.overrides.set("engine.modules", *v);
        } else if (arg == "--frames") {
            const auto v = value();
            if (!v) return std::unexpected(v.error());
            cli.overrides.set("engine.max_frames", *v);
        } else if (arg == "--screenshot") {
            const auto v = value();
            if (!v) return std::unexpected(v.error());
            cli.overrides.set("app.screenshot", *v);
        } else if (arg == "--log-level") {
            const auto v = value();
            if (!v) return std::unexpected(v.error());
            cli.overrides.set("log.level", *v);
        } else if (arg == "--set") {
            const auto v = value();
            if (!v) return std::unexpected(v.error());
            const usize equals = v->find('=');
            if (equals == std::string_view::npos || equals == 0) {
                return fail("--set expects <key>=<value>, got '{}'", *v);
            }
            cli.overrides.set(v->substr(0, equals), v->substr(equals + 1));
        } else {
            return fail("unknown option '{}'", arg);
        }
    }
    return cli;
}

std::string joinModules(const std::vector<std::string>& modules) {
    std::string result;
    for (const std::string& module : modules) {
        if (!result.empty()) {
            result += ",";
        }
        result += module;
    }
    return result;
}

}  // namespace

struct Application::State {
    std::unique_ptr<Engine> engine;
    scene::SceneManager* scenes = nullptr;
    assets::AssetManager* assets = nullptr;
    Subscription closeSubscription;
};

Application::Application() : state_(std::make_unique<State>()) {}

Application::~Application() = default;

Engine& Application::engine() {
    BDEX_ASSERT(state_->engine != nullptr, "the engine only exists while run() is executing");
    return *state_->engine;
}

scene::Scene& Application::scene() {
    scene::Scene* active = state_->scenes != nullptr ? state_->scenes->active() : nullptr;
    BDEX_ASSERT(active != nullptr, "no active scene");
    return *active;
}

assets::AssetManager& Application::assets() {
    BDEX_ASSERT(state_->assets != nullptr);
    return *state_->assets;
}

render::IRenderer& Application::renderer() {
    return engine().services().get<render::IRenderer>();
}

platform::InputState* Application::input() {
    return engine().services().tryGet<platform::InputState>();
}

platform::IWindow* Application::window() {
    auto* windows = engine().services().tryGet<platform::IWindowSystem>();
    return windows != nullptr ? windows->primaryWindow() : nullptr;
}

int Application::run(int argc, char** argv) {
    const std::filesystem::path executable = fs::executablePath();
    const std::filesystem::path executableDir = executable.parent_path();

    // --- Configuration: defaults < onConfigure < file < command line ---------
    const auto cli = parseCommandLine(argc, argv);
    if (!cli) {
        std::fprintf(stderr, "error: %s\n\n%s", cli.error().message.c_str(), kUsage.data());
        return kExitBadCommandLine;
    }
    if (cli->help) {
        std::printf("Usage: %s [options]\n\n%s", executable.filename().string().c_str(),
                    kUsage.data());
        return kExitSuccess;
    }

    Config config;
    config.set("app.name", executable.stem().string());
    config.set("engine.modules", "window_glfw,rhi_vulkan,renderer_forward");
    onConfigure(config);

    std::filesystem::path configFile = executableDir / (executable.stem().string() + ".ini");
    const bool explicitConfig = cli->configFile.has_value();
    if (explicitConfig) {
        configFile = *cli->configFile;
    }
    std::error_code ec;
    if (explicitConfig || std::filesystem::exists(configFile, ec)) {
        const auto fileConfig = Config::loadFile(configFile);
        if (!fileConfig) {
            std::fprintf(stderr, "error: %s\n", fileConfig.error().message.c_str());
            return kExitStartupFailure;
        }
        config.merge(*fileConfig);
    }
    config.merge(cli->overrides);

    if (log::Level level; log::parseLevel(config.getString("log.level", "info"), level)) {
        log::setLevel(level);
    } else {
        log::warn("app", "unknown log level '{}'", config.getString("log.level"));
    }

    // Headless: keep the configured stack but swap the window backend.
    std::vector<std::string> modules = config.getList("engine.modules");
    if (config.getBool("app.headless", false)) {
        for (std::string& module : modules) {
            if (module.starts_with("window_")) {
                module = "window_null";
            }
        }
        config.set("engine.modules", joinModules(modules));
    }

    // A capture implies a bounded run when no frame count was given.
    const std::string screenshotPath = config.getString("app.screenshot");
    if (!screenshotPath.empty() && config.getInt("engine.max_frames", 0) <= 0) {
        config.set("engine.max_frames", "3");
    }

    // --- Engine and common services -------------------------------------------
    state_->engine = std::make_unique<Engine>(config);
    Engine& engine = *state_->engine;
    ServiceRegistry& services = engine.services();

    auto vfs = std::make_shared<VirtualFileSystem>();
    vfs->mount("engine", executableDir / "assets" / "engine");
    vfs->mount("app", executableDir / "assets" / config.getString("app.name"));
    services.provide<VirtualFileSystem>(vfs);

    services.provide<JobSystem>(std::make_shared<JobSystem>(
        static_cast<u32>(std::max<i64>(config.getInt("jobs.threads", 0), 0))));

    auto assetManager = std::make_shared<assets::AssetManager>(*vfs);
    state_->assets = assetManager.get();
    services.provide<assets::AssetManager>(assetManager);

    auto sceneManager = std::make_shared<scene::SceneManager>();
    state_->scenes = sceneManager.get();
    sceneManager->create("main");
    services.provide<scene::SceneManager>(sceneManager);

    // --- Modules -----------------------------------------------------------------
    engine.modules().addSearchPath(executableDir);
    for (const std::string& module : modules) {
        if (const auto loaded = engine.modules().load(module); !loaded) {
            log::error("app", "{}", loaded.error().message);
            return kExitStartupFailure;
        }
    }
    if (const auto initialized = engine.initialize(); !initialized) {
        log::error("app", "{}", initialized.error().message);
        return kExitStartupFailure;
    }

    // --- Frame tasks ---------------------------------------------------------------
    Scheduler& scheduler = engine.scheduler();
    scheduler.add(Phase::Update, "app.update", [this](const FrameTime& time) { onUpdate(time); });
    scheduler.add(Phase::FixedUpdate, "app.fixed_update",
                  [this](const FrameTime& time) { onFixedUpdate(time); });
    scheduler.add(
        Phase::PostUpdate, "scene.world_transforms",
        [this](const FrameTime&) {
            if (scene::Scene* active = state_->scenes->active()) {
                scene::updateWorldTransforms(active->world());
            }
        },
        1000);

    if (!screenshotPath.empty()) {
        // Capture the requested frame, or the last one of a bounded run.
        const i64 requestedFrame = config.getInt("app.screenshot_frame", -1);
        const u64 lastFrame =
            static_cast<u64>(std::max<i64>(config.getInt("engine.max_frames", 1), 1)) - 1;
        const u64 screenshotFrame =
            requestedFrame >= 0 ? static_cast<u64>(requestedFrame) : lastFrame;
        scheduler.add(Phase::PreRender, "app.screenshot",
                      [this, screenshotPath, screenshotFrame](const FrameTime& time) {
                          if (time.frame != screenshotFrame) {
                              return;
                          }
                          if (auto* target = this->engine().services().tryGet<render::IRenderer>()) {
                              target->requestScreenshot(screenshotPath);
                          } else {
                              log::warn("app", "--screenshot ignored: no renderer module loaded");
                          }
                      });
    }

    state_->closeSubscription = engine.events().subscribe<platform::WindowCloseRequested>(
        [&engine](const platform::WindowCloseRequested&) { engine.requestExit(); });

    // --- Run -------------------------------------------------------------------------
    int exitCode = kExitSuccess;
    if (const auto started = onStart(); started) {
        log::info("app", "running ({} modules: {})", modules.size(), joinModules(modules));
        engine.run();
        log::info("app", "stopping after {} frames", engine.time().frame);
    } else {
        log::error("app", "startup failed: {}", started.error().message);
        exitCode = kExitStartupFailure;
    }
    onStop();

    if (const auto* device = services.tryGet<rhi::IDevice>(); device != nullptr) {
        const u32 validationErrors = device->validationErrorCount();
        if (validationErrors > 0) {
            log::error("app", "the RHI reported {} validation error(s)", validationErrors);
            if (config.getBool("app.strict", false) && exitCode == kExitSuccess) {
                exitCode = kExitValidationErrors;
            }
        }
    }

    state_->closeSubscription.reset();
    engine.shutdown();
    state_->scenes = nullptr;
    state_->assets = nullptr;
    state_->engine.reset();
    return exitCode;
}

}  // namespace bdex::app
