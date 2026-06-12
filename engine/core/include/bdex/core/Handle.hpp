#pragma once

#include <bdex/core/Types.hpp>

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace bdex {

/// Typed, generational handle. A handle is a plain value: cheap to copy, safe
/// to keep after the resource is destroyed (lookups simply fail), and free of
/// any vtable, which makes it a good currency across module boundaries.
template <class Tag>
struct Handle {
    static constexpr u32 kInvalidIndex = ~0u;

    u32 index = kInvalidIndex;
    u32 generation = 0;

    [[nodiscard]] constexpr bool valid() const { return index != kInvalidIndex; }
    constexpr explicit operator bool() const { return valid(); }
    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

/// Slot map storing values of type T addressed by Handle<Tag>.
/// Slots are recycled; a stale handle is detected through its generation.
template <class T, class Tag>
class HandlePool {
public:
    using HandleType = Handle<Tag>;

    HandleType insert(T value) {
        u32 index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<u32>(slots_.size());
            slots_.emplace_back();
        }
        Slot& slot = slots_[index];
        slot.value.emplace(std::move(value));
        ++count_;
        return HandleType{index, slot.generation};
    }

    /// Removes and returns the value, or nullopt if the handle is stale.
    std::optional<T> take(HandleType handle) {
        if (!contains(handle)) {
            return std::nullopt;
        }
        Slot& slot = slots_[handle.index];
        std::optional<T> value = std::move(slot.value);
        slot.value.reset();
        ++slot.generation;
        free_.push_back(handle.index);
        --count_;
        return value;
    }

    bool remove(HandleType handle) { return take(handle).has_value(); }

    [[nodiscard]] bool contains(HandleType handle) const {
        return handle.index < slots_.size() && slots_[handle.index].generation == handle.generation &&
               slots_[handle.index].value.has_value();
    }

    [[nodiscard]] T* get(HandleType handle) {
        return contains(handle) ? &*slots_[handle.index].value : nullptr;
    }
    [[nodiscard]] const T* get(HandleType handle) const {
        return contains(handle) ? &*slots_[handle.index].value : nullptr;
    }

    [[nodiscard]] usize size() const { return count_; }
    [[nodiscard]] bool empty() const { return count_ == 0; }

    /// Calls fn(handle, value) for every live value.
    template <class Fn>
    void forEach(Fn&& fn) {
        for (u32 index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            if (slot.value.has_value()) {
                fn(HandleType{index, slot.generation}, *slot.value);
            }
        }
    }

    void clear() {
        slots_.clear();
        free_.clear();
        count_ = 0;
    }

private:
    struct Slot {
        std::optional<T> value;
        u32 generation = 1;
    };

    std::vector<Slot> slots_;
    std::vector<u32> free_;
    usize count_ = 0;
};

}  // namespace bdex

template <class Tag>
struct std::hash<bdex::Handle<Tag>> {
    std::size_t operator()(const bdex::Handle<Tag>& handle) const noexcept {
        return std::hash<bdex::u64>{}((static_cast<bdex::u64>(handle.generation) << 32) |
                                      handle.index);
    }
};
