#include <bdex/core/Scheduler.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace bdex;

TEST(Scheduler, RunsTasksByOrderThenRegistration) {
    Scheduler scheduler;
    std::vector<std::string> trace;
    scheduler.add(Phase::Update, "late", [&](const FrameTime&) { trace.push_back("late"); }, 10);
    scheduler.add(Phase::Update, "a", [&](const FrameTime&) { trace.push_back("a"); });
    scheduler.add(Phase::Update, "early", [&](const FrameTime&) { trace.push_back("early"); }, -5);
    scheduler.add(Phase::Update, "b", [&](const FrameTime&) { trace.push_back("b"); });
    scheduler.add(Phase::Render, "render", [&](const FrameTime&) { trace.push_back("render"); });

    scheduler.run(Phase::Update, {});
    EXPECT_EQ(trace, (std::vector<std::string>{"early", "a", "b", "late"}));
    EXPECT_EQ(scheduler.taskNames(Phase::Update),
              (std::vector<std::string>{"early", "a", "b", "late"}));
    EXPECT_EQ(scheduler.taskCount(), 5u);
}

TEST(Scheduler, TaskCanRemoveItselfWhileRunning) {
    Scheduler scheduler;
    int runs = 0;
    TaskId id = 0;
    id = scheduler.add(Phase::Update, "once", [&](const FrameTime&) {
        ++runs;
        scheduler.remove(id);
    });
    int other = 0;
    scheduler.add(Phase::Update, "always", [&](const FrameTime&) { ++other; });

    scheduler.run(Phase::Update, {});
    scheduler.run(Phase::Update, {});

    EXPECT_EQ(runs, 1);
    EXPECT_EQ(other, 2);
    EXPECT_EQ(scheduler.taskCount(), 1u);
}

TEST(Scheduler, ScopedTaskRemovesOnDestruction) {
    Scheduler scheduler;
    int runs = 0;
    {
        const ScopedTask task(scheduler,
                              scheduler.add(Phase::Input, "scoped", [&](const FrameTime&) { ++runs; }));
        scheduler.run(Phase::Input, {});
    }
    scheduler.run(Phase::Input, {});
    EXPECT_EQ(runs, 1);
}

TEST(Scheduler, PassesFrameTimeThrough) {
    Scheduler scheduler;
    FrameTime seen;
    scheduler.add(Phase::Update, "capture", [&](const FrameTime& time) { seen = time; });
    scheduler.run(Phase::Update, FrameTime{0.5, 2.0, 7});
    EXPECT_DOUBLE_EQ(seen.delta, 0.5);
    EXPECT_DOUBLE_EQ(seen.elapsed, 2.0);
    EXPECT_EQ(seen.frame, 7u);
}
