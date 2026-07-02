#pragma once

#include <bdex/core/Types.hpp>

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace bdex {

/// Fixed-size thread pool for data-parallel work and background tasks.
class JobSystem {
public:
    /// `threadCount == 0` picks hardware_concurrency() - 1 (at least 1).
    explicit JobSystem(u32 threadCount = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] u32 threadCount() const { return static_cast<u32>(workers_.size()); }

    /// Runs `fn` on a worker thread and returns a future for its result.
    template <class Fn>
    auto submit(Fn&& fn) -> std::future<std::invoke_result_t<std::decay_t<Fn>>> {
        using ResultType = std::invoke_result_t<std::decay_t<Fn>>;
        auto task = std::make_shared<std::packaged_task<ResultType()>>(std::forward<Fn>(fn));
        std::future<ResultType> future = task->get_future();
        enqueue([task] { (*task)(); });
        return future;
    }

    /// Splits [0, count) into chunks of at most `grain` items and calls
    /// `fn(begin, end)` for each chunk, in parallel. Blocks until done.
    /// The calling thread takes part in the work, so nesting parallelFor
    /// inside a job cannot deadlock. `fn` must not throw.
    void parallelFor(usize count, usize grain, const std::function<void(usize, usize)>& fn);

private:
    void enqueue(std::function<void()> job);
    void workerLoop();

    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
};

}  // namespace bdex
