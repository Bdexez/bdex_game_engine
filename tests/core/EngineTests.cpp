#include <bdex/core/Engine.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace bdex;

TEST(Engine, RunsPhasesInOrderEveryFrame) {
    Config config;
    config.set("engine.forced_delta", "0.1");      // deterministic time
    config.set("engine.fixed_timestep", "0.05");   // => 2 fixed steps per frame
    Engine engine(config);
    ASSERT_TRUE(engine.initialize().has_value());

    std::vector<std::string> trace;
    for (const Phase phase : {Phase::Render, Phase::Update, Phase::Input, Phase::FixedUpdate,
                              Phase::BeginFrame, Phase::EndFrame, Phase::PostUpdate}) {
        engine.scheduler().add(phase, "trace", [&trace, phase](const FrameTime&) {
            trace.emplace_back(phaseName(phase));
        });
    }

    engine.tick();
    EXPECT_EQ(trace, (std::vector<std::string>{"BeginFrame", "Input", "FixedUpdate", "FixedUpdate",
                                               "Update", "PostUpdate", "Render", "EndFrame"}));
}

TEST(Engine, ForcedDeltaDrivesFrameTime) {
    Config config;
    config.set("engine.forced_delta", "0.25");
    Engine engine(config);
    ASSERT_TRUE(engine.initialize().has_value());

    std::vector<FrameTime> seen;
    engine.scheduler().add(Phase::Update, "capture",
                           [&](const FrameTime& time) { seen.push_back(time); });
    engine.tick();
    engine.tick();

    ASSERT_EQ(seen.size(), 2u);
    EXPECT_DOUBLE_EQ(seen[0].delta, 0.25);
    EXPECT_DOUBLE_EQ(seen[1].elapsed, 0.5);
    EXPECT_EQ(seen[0].frame, 0u);
    EXPECT_EQ(seen[1].frame, 1u);
}

TEST(Engine, FixedUpdateReceivesTheFixedStep) {
    Config config;
    // Values exactly representable in binary, so the accumulator is exact.
    config.set("engine.forced_delta", "0.375");
    config.set("engine.fixed_timestep", "0.25");
    Engine engine(config);
    ASSERT_TRUE(engine.initialize().has_value());

    int steps = 0;
    engine.scheduler().add(Phase::FixedUpdate, "count", [&](const FrameTime& time) {
        EXPECT_DOUBLE_EQ(time.delta, 0.25);
        ++steps;
    });

    // 0.375 s per frame with a 0.25 s step: 1 step, then 2 (0.125 s carried over).
    engine.tick();
    EXPECT_EQ(steps, 1);
    engine.tick();
    EXPECT_EQ(steps, 3);
}

TEST(Engine, StopsOnRequestOrFrameLimit) {
    Config config;
    config.set("engine.max_frames", "5");
    Engine limited(config);
    ASSERT_TRUE(limited.initialize().has_value());
    limited.run();
    EXPECT_EQ(limited.time().frame, 5u);

    Engine engine;
    ASSERT_TRUE(engine.initialize().has_value());
    int frames = 0;
    engine.scheduler().add(Phase::Update, "quit", [&](const FrameTime&) {
        if (++frames == 3) {
            engine.requestExit();
        }
    });
    engine.run();
    EXPECT_EQ(frames, 3);
    EXPECT_TRUE(engine.exitRequested());
}

TEST(Engine, QueuedEventsAreFlushedDuringTheFrame) {
    struct Scored {
        int points;
    };
    Engine engine;
    ASSERT_TRUE(engine.initialize().has_value());

    int total = 0;
    const Subscription subscription =
        engine.events().subscribe<Scored>([&](const Scored& event) { total += event.points; });
    engine.scheduler().add(Phase::Update, "score",
                           [&](const FrameTime&) { engine.events().enqueue(Scored{10}); });

    engine.tick();
    EXPECT_EQ(total, 10);
}
