#include <bdex/core/JobSystem.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <vector>

using namespace bdex;

TEST(JobSystem, SubmitReturnsResults) {
    JobSystem jobs(4);
    EXPECT_EQ(jobs.threadCount(), 4u);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 64; ++i) {
        futures.push_back(jobs.submit([i] { return i * i; }));
    }
    int sum = 0;
    for (auto& future : futures) {
        sum += future.get();
    }
    EXPECT_EQ(sum, 85344);  // sum of squares 0..63
}

TEST(JobSystem, ParallelForCoversEveryIndexExactlyOnce) {
    JobSystem jobs(4);
    std::vector<std::atomic<int>> hits(10'000);

    jobs.parallelFor(hits.size(), 128, [&](usize begin, usize end) {
        for (usize i = begin; i < end; ++i) {
            hits[i].fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (const auto& hit : hits) {
        ASSERT_EQ(hit.load(), 1);
    }
}

TEST(JobSystem, ParallelForCanBeNestedInsideAJob) {
    JobSystem jobs(2);
    std::atomic<int> total{0};

    // Saturate the pool with jobs that each run their own parallelFor.
    std::vector<std::future<void>> futures;
    for (int job = 0; job < 8; ++job) {
        futures.push_back(jobs.submit([&] {
            jobs.parallelFor(100, 10, [&](usize begin, usize end) {
                total.fetch_add(static_cast<int>(end - begin), std::memory_order_relaxed);
            });
        }));
    }
    for (auto& future : futures) {
        future.get();
    }
    EXPECT_EQ(total.load(), 800);
}

TEST(JobSystem, ParallelForHandlesTrivialRanges) {
    JobSystem jobs(2);
    int calls = 0;
    jobs.parallelFor(0, 16, [&](usize, usize) { ++calls; });
    EXPECT_EQ(calls, 0);
    jobs.parallelFor(5, 16, [&](usize begin, usize end) {
        ++calls;
        EXPECT_EQ(begin, 0u);
        EXPECT_EQ(end, 5u);
    });
    EXPECT_EQ(calls, 1);
}
