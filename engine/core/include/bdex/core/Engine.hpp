#pragma once

#include <bdex/core/Config.hpp>
#include <bdex/core/EventBus.hpp>
#include <bdex/core/ModuleManager.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/core/Scheduler.hpp>
#include <bdex/core/ServiceRegistry.hpp>
#include <bdex/core/Time.hpp>

#include <chrono>

namespace bdex {

/// The engine kernel.
///
/// The kernel knows nothing about windows, GPUs or scenes. It owns the four
/// mechanisms modules are built from, and a loop that runs the frame phases:
///
///   - ModuleManager   : loads modules and orders their initialization
///   - ServiceRegistry : interface -> implementation lookup
///   - EventBus        : decoupled notifications
///   - Scheduler       : per-frame tasks, grouped by Phase
///
/// Everything else is contributed by modules.
///
/// Recognized configuration keys:
///   engine.fixed_timestep  seconds per FixedUpdate step (default 1/60)
///   engine.max_frames      exit after N frames, 0 = unlimited (default 0)
///   engine.forced_delta    if > 0, use this delta instead of the wall clock
///                          (deterministic runs, captures, tests)
class Engine {
public:
    explicit Engine(Config config = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] Config& config() { return config_; }
    [[nodiscard]] const Config& config() const { return config_; }
    [[nodiscard]] ServiceRegistry& services() { return services_; }
    [[nodiscard]] EventBus& events() { return events_; }
    [[nodiscard]] Scheduler& scheduler() { return scheduler_; }
    [[nodiscard]] ModuleManager& modules() { return modules_; }
    [[nodiscard]] const FrameTime& time() const { return time_; }

    /// Initializes the loaded modules in dependency order.
    Result<> initialize();

    /// Runs frames until requestExit() is called or engine.max_frames is hit.
    void run();

    /// Runs a single frame. Returns false once the engine should stop.
    bool tick();

    void requestExit() { exitRequested_ = true; }
    [[nodiscard]] bool exitRequested() const { return exitRequested_; }

    /// Shuts modules down, then releases tasks, events, services and plugin
    /// libraries, in that order. Idempotent; also called by the destructor.
    void shutdown();

private:
    using Clock = std::chrono::steady_clock;

    // modules_ is declared first so it is destroyed last: plugin libraries
    // must stay mapped while services, events and tasks (which may hold code
    // from plugins) are destroyed.
    ModuleManager modules_;
    Config config_;
    ServiceRegistry services_;
    EventBus events_;
    Scheduler scheduler_;

    FrameTime time_;
    Clock::time_point lastTick_;
    f64 fixedTimestep_ = 1.0 / 60.0;
    f64 fixedAccumulator_ = 0.0;
    f64 forcedDelta_ = 0.0;
    u64 maxFrames_ = 0;
    bool firstTick_ = true;
    bool exitRequested_ = false;
    bool shutDown_ = false;
};

}  // namespace bdex
