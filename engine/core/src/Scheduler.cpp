#include <bdex/core/Scheduler.hpp>

#include <algorithm>

namespace bdex {

std::string_view phaseName(Phase phase) {
    switch (phase) {
        case Phase::BeginFrame: return "BeginFrame";
        case Phase::Input: return "Input";
        case Phase::PreUpdate: return "PreUpdate";
        case Phase::FixedUpdate: return "FixedUpdate";
        case Phase::Update: return "Update";
        case Phase::PostUpdate: return "PostUpdate";
        case Phase::PreRender: return "PreRender";
        case Phase::Render: return "Render";
        case Phase::PostRender: return "PostRender";
        case Phase::EndFrame: return "EndFrame";
        case Phase::Count: break;
    }
    return "?";
}

TaskId Scheduler::add(Phase phase, std::string name, Task task, i32 order) {
    auto& entries = phases_[static_cast<usize>(phase)];
    const TaskId id = nextId_++;
    // Insert after every entry with an order <= ours: stable for equal orders.
    // (When called from a running task of the same phase the new task may or
    // may not run this frame; it always runs from the next frame on.)
    const auto position = std::upper_bound(
        entries.begin(), entries.end(), order,
        [](i32 value, const Entry& entry) { return value < entry.order; });
    entries.insert(position, Entry{id, order, std::move(name), std::move(task)});
    return id;
}

void Scheduler::remove(TaskId id) {
    for (auto& entries : phases_) {
        for (Entry& entry : entries) {
            if (entry.id == id) {
                entry.id = 0;  // tombstone, skipped by run()
                if (runDepth_ == 0) {
                    std::erase_if(entries, [](const Entry& e) { return e.id == 0; });
                } else {
                    needsCompaction_ = true;
                }
                return;
            }
        }
    }
}

void Scheduler::clear() {
    for (auto& entries : phases_) {
        entries.clear();
    }
    needsCompaction_ = false;
}

void Scheduler::run(Phase phase, const FrameTime& time) {
    auto& entries = phases_[static_cast<usize>(phase)];
    ++runDepth_;
    // Index-based: a task may add tasks (vector growth) while we iterate.
    for (usize i = 0; i < entries.size(); ++i) {
        if (entries[i].id != 0) {
            const Task task = entries[i].task;  // copy: the task may remove itself
            task(time);
        }
    }
    --runDepth_;

    if (runDepth_ == 0 && needsCompaction_) {
        for (auto& list : phases_) {
            std::erase_if(list, [](const Entry& e) { return e.id == 0; });
        }
        needsCompaction_ = false;
    }
}

usize Scheduler::taskCount() const {
    usize count = 0;
    for (const auto& entries : phases_) {
        count += static_cast<usize>(
            std::count_if(entries.begin(), entries.end(), [](const Entry& e) { return e.id != 0; }));
    }
    return count;
}

std::vector<std::string> Scheduler::taskNames(Phase phase) const {
    std::vector<std::string> names;
    for (const Entry& entry : phases_[static_cast<usize>(phase)]) {
        if (entry.id != 0) {
            names.push_back(entry.name);
        }
    }
    return names;
}

}  // namespace bdex
