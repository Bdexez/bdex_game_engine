#include <bdex/core/JobSystem.hpp>

#include <algorithm>
#include <atomic>

namespace bdex {

JobSystem::JobSystem(u32 threadCount) {
    if (threadCount == 0) {
        const u32 hardware = std::thread::hardware_concurrency();
        threadCount = hardware > 1 ? hardware - 1 : 1;
    }
    workers_.reserve(threadCount);
    for (u32 i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) {
        worker.join();
    }
}

void JobSystem::enqueue(std::function<void()> job) {
    {
        std::lock_guard lock(mutex_);
        queue_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void JobSystem::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping, and every queued job has been drained
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job();
    }
}

void JobSystem::parallelFor(usize count, usize grain,
                            const std::function<void(usize, usize)>& fn) {
    if (count == 0) {
        return;
    }
    grain = std::max<usize>(grain, 1);
    const usize chunks = (count + grain - 1) / grain;
    if (chunks == 1) {
        fn(0, count);
        return;
    }

    // Chunks are claimed through an atomic cursor by the workers *and* the
    // caller. `fn` is only dereferenced after a successful claim, and the
    // caller does not return before every claimed chunk has completed, so the
    // reference stays valid for as long as it can be used.
    struct Shared {
        std::atomic<usize> next{0};
        std::atomic<usize> completed{0};
        usize chunks = 0;
        usize count = 0;
        usize grain = 0;
        const std::function<void(usize, usize)>* fn = nullptr;
        std::mutex mutex;
        std::condition_variable done;
    };
    const auto shared = std::make_shared<Shared>();
    shared->chunks = chunks;
    shared->count = count;
    shared->grain = grain;
    shared->fn = &fn;

    const auto work = [shared] {
        for (;;) {
            const usize chunk = shared->next.fetch_add(1, std::memory_order_relaxed);
            if (chunk >= shared->chunks) {
                return;
            }
            const usize begin = chunk * shared->grain;
            const usize end = std::min(shared->count, begin + shared->grain);
            (*shared->fn)(begin, end);
            if (shared->completed.fetch_add(1, std::memory_order_acq_rel) + 1 == shared->chunks) {
                std::lock_guard lock(shared->mutex);
                shared->done.notify_all();
            }
        }
    };

    const usize helpers = std::min<usize>(workers_.size(), chunks - 1);
    for (usize i = 0; i < helpers; ++i) {
        enqueue(work);
    }
    work();

    std::unique_lock lock(shared->mutex);
    shared->done.wait(lock, [&] {
        return shared->completed.load(std::memory_order_acquire) == shared->chunks;
    });
}

}  // namespace bdex
