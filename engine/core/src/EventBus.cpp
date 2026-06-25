#include <bdex/core/EventBus.hpp>

#include <algorithm>

namespace bdex {
namespace detail {

struct EventBusState {
    struct Slot {
        u64 id = 0;  // 0 = removed, waiting for compaction
        std::function<void(const void*)> handler;
    };

    std::unordered_map<TypeId, std::vector<Slot>> handlers;
    u64 nextId = 1;
    u32 dispatchDepth = 0;
    bool needsCompaction = false;

    void remove(TypeId type, u64 id) {
        const auto it = handlers.find(type);
        if (it == handlers.end()) {
            return;
        }
        for (Slot& slot : it->second) {
            if (slot.id == id) {
                slot.id = 0;
                if (dispatchDepth == 0) {
                    // Not dispatching: the slot can be erased right away.
                    std::erase_if(it->second, [](const Slot& s) { return s.id == 0; });
                } else {
                    // A handler is running: keep the vector stable, drop the
                    // callable later.
                    needsCompaction = true;
                }
                return;
            }
        }
    }

    void compact() {
        for (auto& [type, slots] : handlers) {
            std::erase_if(slots, [](const Slot& s) { return s.id == 0; });
        }
        needsCompaction = false;
    }
};

}  // namespace detail

// --- Subscription ----------------------------------------------------------

Subscription::Subscription(std::weak_ptr<detail::EventBusState> state, TypeId type, u64 id)
    : state_(std::move(state)), type_(type), id_(id) {}

Subscription::Subscription(Subscription&& other) noexcept
    : state_(std::move(other.state_)), type_(other.type_), id_(std::exchange(other.id_, 0)) {}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        type_ = other.type_;
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

Subscription::~Subscription() {
    reset();
}

void Subscription::reset() {
    if (id_ != 0) {
        if (const auto state = state_.lock()) {
            state->remove(type_, id_);
        }
        id_ = 0;
    }
    state_.reset();
}

// --- EventBus --------------------------------------------------------------

EventBus::EventBus() : state_(std::make_shared<detail::EventBusState>()) {}

EventBus::~EventBus() = default;

Subscription EventBus::subscribeRaw(TypeId type, RawHandler handler) {
    const u64 id = state_->nextId++;
    state_->handlers[type].push_back({id, std::move(handler)});
    return Subscription(state_, type, id);
}

void EventBus::publishRaw(TypeId type, const void* event) {
    // Keep the state alive even if a handler destroys the bus owner's tokens.
    const std::shared_ptr<detail::EventBusState> state = state_;
    const auto it = state->handlers.find(type);
    if (it == state->handlers.end()) {
        return;
    }

    ++state->dispatchDepth;
    // Index-based loop over the size at entry: handlers subscribed during the
    // dispatch are not called for this event, and push_back cannot invalidate
    // the iteration. unordered_map never moves its values on rehash, so `it`
    // stays valid if a handler subscribes to a new event type.
    const usize count = it->second.size();
    for (usize i = 0; i < count; ++i) {
        if (it->second[i].id != 0) {
            // Copy the callable: the handler may unsubscribe itself.
            const auto handler = it->second[i].handler;
            handler(event);
        }
    }
    --state->dispatchDepth;

    if (state->dispatchDepth == 0 && state->needsCompaction) {
        state->compact();
    }
}

void EventBus::dispatchQueued() {
    // Events enqueued by handlers are delivered on the next call.
    std::vector<std::function<void()>> pending;
    pending.swap(queued_);
    for (const auto& deliver : pending) {
        deliver();
    }
}

void EventBus::clear() {
    queued_.clear();
    // Swap in a fresh state: outstanding tokens point to the old one, which
    // dies with them.
    state_ = std::make_shared<detail::EventBusState>();
}

}  // namespace bdex
