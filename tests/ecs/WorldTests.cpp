#include <bdex/ecs/World.hpp>

#include <gtest/gtest.h>

#include <set>
#include <string>

using namespace bdex;
using namespace bdex::ecs;

namespace {
struct Position {
    float x = 0, y = 0;
};
struct Velocity {
    float x = 0, y = 0;
};
struct Tag {};
struct Label {
    std::string text;
};
}  // namespace

TEST(World, CreateDestroyAndGenerations) {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    EXPECT_NE(a, b);
    EXPECT_TRUE(world.alive(a));
    EXPECT_EQ(world.entityCount(), 2u);

    world.destroy(a);
    EXPECT_FALSE(world.alive(a));
    EXPECT_EQ(world.entityCount(), 1u);

    const Entity c = world.create();  // reuses a's slot
    EXPECT_EQ(c.index, a.index);
    EXPECT_NE(c.generation, a.generation);
    EXPECT_FALSE(world.alive(a));
    EXPECT_TRUE(world.alive(c));

    world.destroy(a);  // stale handle: no effect
    EXPECT_TRUE(world.alive(c));
    EXPECT_FALSE(world.alive(Entity{}));
}

TEST(World, ComponentsLifecycle) {
    World world;
    const Entity e = world.create();

    world.emplace<Position>(e, 1.0f, 2.0f);
    EXPECT_TRUE(world.has<Position>(e));
    EXPECT_FALSE((world.has<Position, Velocity>(e)));
    EXPECT_FLOAT_EQ(world.get<Position>(e).y, 2.0f);

    world.emplace<Position>(e, 5.0f, 6.0f);  // replace
    EXPECT_FLOAT_EQ(world.get<Position>(e).x, 5.0f);

    EXPECT_EQ(world.tryGet<Velocity>(e), nullptr);
    world.remove<Position>(e);
    EXPECT_FALSE(world.has<Position>(e));
}

TEST(World, DestroyRemovesComponentsAndKeepsOthersIntact) {
    World world;
    const Entity a = world.create();
    const Entity b = world.create();
    const Entity c = world.create();
    world.emplace<Label>(a, "a");
    world.emplace<Label>(b, "b");
    world.emplace<Label>(c, "c");

    world.destroy(a);  // swap-and-pop moves c's component into a's slot

    EXPECT_EQ(world.get<Label>(b).text, "b");
    EXPECT_EQ(world.get<Label>(c).text, "c");
    EXPECT_EQ(world.view<Label>().count(), 2u);

    const Entity reused = world.create();
    EXPECT_FALSE(world.has<Label>(reused));  // no component leaks to the new entity
}

TEST(World, ViewIteratesEntitiesWithAllComponents) {
    World world;
    const Entity moving = world.create();
    world.emplace<Position>(moving, 0.0f, 0.0f);
    world.emplace<Velocity>(moving, 1.0f, 2.0f);

    const Entity still = world.create();
    world.emplace<Position>(still, 10.0f, 10.0f);

    int visited = 0;
    world.view<Position, Velocity>().each([&](Entity entity, Position& p, Velocity& v) {
        EXPECT_EQ(entity, moving);
        p.x += v.x;
        p.y += v.y;
        ++visited;
    });

    EXPECT_EQ(visited, 1);
    EXPECT_FLOAT_EQ(world.get<Position>(moving).y, 2.0f);
    EXPECT_FLOAT_EQ(world.get<Position>(still).x, 10.0f);

    // The callback may omit the entity parameter.
    int count = 0;
    world.view<Position>().each([&](Position&) { ++count; });
    EXPECT_EQ(count, 2);
}

TEST(World, EntitiesCanBeDestroyedWhileIterating) {
    World world;
    std::set<u32> expected;
    for (int i = 0; i < 100; ++i) {
        const Entity e = world.create();
        world.emplace<Tag>(e);
        world.emplace<Position>(e, static_cast<float>(i), 0.0f);
        if (i % 2 == 1) {
            expected.insert(e.index);
        }
    }

    int visited = 0;
    world.view<Tag, Position>().each([&](Entity entity, Tag&, Position& p) {
        ++visited;
        if (static_cast<int>(p.x) % 2 == 0) {
            world.destroy(entity);
        }
    });
    EXPECT_EQ(visited, 100);  // each entity seen exactly once

    std::set<u32> remaining;
    world.view<Tag>().each([&](Entity entity, Tag&) { remaining.insert(entity.index); });
    EXPECT_EQ(remaining, expected);
}

TEST(World, ClearInvalidatesEverything) {
    World world;
    const Entity e = world.create();
    world.emplace<Position>(e);
    world.clear();

    EXPECT_FALSE(world.alive(e));
    EXPECT_EQ(world.entityCount(), 0u);
    EXPECT_EQ(world.view<Position>().count(), 0u);

    const Entity fresh = world.create();
    EXPECT_TRUE(world.alive(fresh));
    EXPECT_FALSE(world.has<Position>(fresh));
}
