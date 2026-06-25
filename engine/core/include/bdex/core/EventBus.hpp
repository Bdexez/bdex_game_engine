#pragma once

#include <bdex/core/TypeId.hpp>
#include <bdex/core/Types.hpp>

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bdex {

namespace detail {
struct EventBusState;
}

/// RAII subscription token: the handler is removed when the token is destroyed
/// or reset. Outliving the EventBus is safe.
class Subscription {
public:
    Subscription() = default;
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription();

    void reset();
    [[nodiscard]] bool active() const { return id_ != 0; }

private:
    friend class EventBus;
    Subscription(std::weak_ptr<detail::EventBusState> state, TypeId type, u64 id);

    std::weak_ptr<detail::EventBusState> state_;
    TypeId type_ = 0;
    u64 id_ = 0;
};

/// Type-safe publish/subscribe channel used to decouple modules: a module
/// publishes plain structs, others subscribe without knowing who emits them.
///
/// Not thread-safe: publish and subscribe from the main thread only.
/// Handlers may subscribe, unsubscribe and publish while being dispatched.
class EventBus {
public:
    EventBus();
    ~EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    template <class Event>
    [[nodiscard]] Subscription subscribe(std::function<void(const Event&)> handler) {
        return subscribeRaw(typeId<Event>(), [handler = std::move(handler)](const void* event) {
            handler(*static_cast<const Event*>(event));
        });
    }

    /// Dispatches immediately to every current subscriber.
    template <class Event>
    void publish(const Event& event) {
        publishRaw(typeId<Event>(), &event);
    }

    /// Defers the event until the next dispatchQueued() (once per frame).
    template <class Event>
    void enqueue(Event event) {
        queued_.push_back([this, event = std::move(event)] { publish(event); });
    }

    void dispatchQueued();

    /// Drops every handler and queued event. Existing tokens become inert.
    void clear();

private:
    using RawHandler = std::function<void(const void*)>;

    Subscription subscribeRaw(TypeId type, RawHandler handler);
    void publishRaw(TypeId type, const void* event);

    std::shared_ptr<detail::EventBusState> state_;
    std::vector<std::function<void()>> queued_;
};

}  // namespace bdex
