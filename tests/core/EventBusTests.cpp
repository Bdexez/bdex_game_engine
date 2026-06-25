#include <bdex/core/EventBus.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace bdex;

namespace {
struct Damage {
    int amount = 0;
};
struct Heal {
    int amount = 0;
};
}  // namespace

TEST(EventBus, DeliversToMatchingSubscribersOnly) {
    EventBus bus;
    int damage = 0;
    int heal = 0;
    const Subscription a = bus.subscribe<Damage>([&](const Damage& e) { damage += e.amount; });
    const Subscription b = bus.subscribe<Heal>([&](const Heal& e) { heal += e.amount; });

    bus.publish(Damage{5});
    bus.publish(Damage{2});
    bus.publish(Heal{1});

    EXPECT_EQ(damage, 7);
    EXPECT_EQ(heal, 1);
}

TEST(EventBus, SubscriptionTokenUnsubscribesOnDestruction) {
    EventBus bus;
    int calls = 0;
    {
        const Subscription token = bus.subscribe<Damage>([&](const Damage&) { ++calls; });
        bus.publish(Damage{});
    }
    bus.publish(Damage{});
    EXPECT_EQ(calls, 1);
}

TEST(EventBus, HandlerMayUnsubscribeItselfDuringDispatch) {
    EventBus bus;
    int calls = 0;
    Subscription token;
    token = bus.subscribe<Damage>([&](const Damage&) {
        ++calls;
        token.reset();
    });
    int other = 0;
    const Subscription keep = bus.subscribe<Damage>([&](const Damage&) { ++other; });

    bus.publish(Damage{});
    bus.publish(Damage{});

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(other, 2);
}

TEST(EventBus, HandlerMaySubscribeDuringDispatch) {
    EventBus bus;
    int late = 0;
    std::vector<Subscription> tokens;
    const Subscription first = bus.subscribe<Damage>([&](const Damage&) {
        tokens.push_back(bus.subscribe<Damage>([&](const Damage&) { ++late; }));
    });

    bus.publish(Damage{});  // the new handler is not called for this event
    EXPECT_EQ(late, 0);
    bus.publish(Damage{});
    EXPECT_EQ(late, 1);
}

TEST(EventBus, QueuedEventsAreDeliveredOnDispatch) {
    EventBus bus;
    int total = 0;
    const Subscription token = bus.subscribe<Damage>([&](const Damage& e) { total += e.amount; });

    bus.enqueue(Damage{3});
    bus.enqueue(Damage{4});
    EXPECT_EQ(total, 0);
    bus.dispatchQueued();
    EXPECT_EQ(total, 7);
    bus.dispatchQueued();
    EXPECT_EQ(total, 7);
}

TEST(EventBus, TokenOutlivingTheBusIsHarmless) {
    Subscription token;
    {
        EventBus bus;
        token = bus.subscribe<Damage>([](const Damage&) {});
    }
    token.reset();  // must not touch freed memory
    SUCCEED();
}

TEST(EventBus, ClearDropsHandlers) {
    EventBus bus;
    int calls = 0;
    Subscription token = bus.subscribe<Damage>([&](const Damage&) { ++calls; });
    bus.clear();
    bus.publish(Damage{});
    EXPECT_EQ(calls, 0);
    token.reset();
}
