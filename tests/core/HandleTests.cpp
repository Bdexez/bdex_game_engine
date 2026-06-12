#include <bdex/core/Handle.hpp>

#include <gtest/gtest.h>

#include <string>

using namespace bdex;

namespace {
struct ThingTag {};
using ThingHandle = Handle<ThingTag>;
}  // namespace

TEST(HandlePool, InsertGetRemove) {
    HandlePool<std::string, ThingTag> pool;
    const ThingHandle a = pool.insert("a");
    const ThingHandle b = pool.insert("b");

    ASSERT_TRUE(a.valid());
    EXPECT_NE(a, b);
    EXPECT_EQ(pool.size(), 2u);
    EXPECT_EQ(*pool.get(a), "a");
    EXPECT_EQ(*pool.get(b), "b");

    EXPECT_TRUE(pool.remove(a));
    EXPECT_FALSE(pool.remove(a));
    EXPECT_EQ(pool.get(a), nullptr);
    EXPECT_EQ(pool.size(), 1u);
}

TEST(HandlePool, StaleHandleIsRejectedAfterSlotReuse) {
    HandlePool<int, ThingTag> pool;
    const ThingHandle first = pool.insert(1);
    pool.remove(first);
    const ThingHandle second = pool.insert(2);

    EXPECT_EQ(first.index, second.index);  // slot recycled
    EXPECT_NE(first.generation, second.generation);
    EXPECT_FALSE(pool.contains(first));
    EXPECT_EQ(*pool.get(second), 2);
}

TEST(HandlePool, DefaultHandleIsInvalid) {
    HandlePool<int, ThingTag> pool;
    EXPECT_FALSE(ThingHandle{}.valid());
    EXPECT_EQ(pool.get(ThingHandle{}), nullptr);
}

TEST(HandlePool, ForEachVisitsLiveValuesOnly) {
    HandlePool<int, ThingTag> pool;
    pool.insert(1);
    const ThingHandle removed = pool.insert(2);
    pool.insert(3);
    pool.remove(removed);

    int sum = 0;
    pool.forEach([&](ThingHandle, int& value) { sum += value; });
    EXPECT_EQ(sum, 4);
}
