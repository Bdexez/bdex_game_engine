#include <bdex/core/Engine.hpp>

#include <bdex/core/Log.hpp>

#include <algorithm>

namespace bdex {
namespace {

// A long stall (debugger, window drag) must not explode the simulation.
constexpr f64 kMaxFrameDelta = 0.25;
constexpr u32 kMaxFixedStepsPerFrame = 8;

}  // namespace

Engine::Engine(Config config) : config_(std::move(config)) {}

Engine::~Engine() {
    shutdown();
}

Result<> Engine::initialize() {
    fixedTimestep_ = std::max(config_.getFloat("engine.fixed_timestep", 1.0 / 60.0), 1e-4);
    forcedDelta_ = std::max(config_.getFloat("engine.forced_delta", 0.0), 0.0);
    maxFrames_ = static_cast<u64>(std::max<i64>(config_.getInt("engine.max_frames", 0), 0));
    return modules_.initializeAll(*this);
}

void Engine::run() {
    while (tick()) {
    }
}

bool Engine::tick() {
    const Clock::time_point now = Clock::now();
    if (firstTick_) {
        lastTick_ = now;
        firstTick_ = false;
    }
    const f64 wallDelta = std::chrono::duration<f64>(now - lastTick_).count();
    lastTick_ = now;
    time_.delta = forcedDelta_ > 0.0 ? forcedDelta_ : std::min(wallDelta, kMaxFrameDelta);
    time_.elapsed += time_.delta;

    scheduler_.run(Phase::BeginFrame, time_);
    scheduler_.run(Phase::Input, time_);
    scheduler_.run(Phase::PreUpdate, time_);

    // Fixed-step simulation: consume the accumulated time in constant slices.
    fixedAccumulator_ += time_.delta;
    FrameTime fixedTime = time_;
    fixedTime.delta = fixedTimestep_;
    u32 steps = 0;
    while (fixedAccumulator_ >= fixedTimestep_ && steps < kMaxFixedStepsPerFrame) {
        scheduler_.run(Phase::FixedUpdate, fixedTime);
        fixedAccumulator_ -= fixedTimestep_;
        ++steps;
    }
    if (steps == kMaxFixedStepsPerFrame) {
        fixedAccumulator_ = 0.0;  // cannot keep up: drop the backlog
    }

    scheduler_.run(Phase::Update, time_);
    scheduler_.run(Phase::PostUpdate, time_);
    events_.dispatchQueued();
    scheduler_.run(Phase::PreRender, time_);
    scheduler_.run(Phase::Render, time_);
    scheduler_.run(Phase::PostRender, time_);
    scheduler_.run(Phase::EndFrame, time_);

    ++time_.frame;
    if (maxFrames_ != 0 && time_.frame >= maxFrames_) {
        exitRequested_ = true;
    }
    return !exitRequested_;
}

void Engine::shutdown() {
    if (shutDown_) {
        return;
    }
    shutDown_ = true;

    modules_.shutdownAll(*this);
    // Anything a plugin left behind is destroyed while its code is still
    // mapped; only then are the libraries released.
    scheduler_.clear();
    events_.clear();
    services_.clear();
    modules_.unloadLibraries();
}

}  // namespace bdex
