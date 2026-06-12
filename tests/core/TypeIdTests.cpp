#include <bdex/core/TypeId.hpp>

#include <gtest/gtest.h>

namespace sample {
struct Position {};
struct Velocity {};
template <class T>
struct Wrapper {};
}  // namespace sample

using namespace bdex;

TEST(TypeId, NamesAreFullyQualified) {
    EXPECT_EQ(typeName<int>(), "int");
    EXPECT_EQ(typeName<sample::Position>(), "sample::Position");
    EXPECT_NE(typeName<sample::Wrapper<int>>().find("sample::Wrapper<int>"), std::string_view::npos);
}

TEST(TypeId, IdsAreStableAndDistinct) {
    static_assert(typeId<sample::Position>() == typeId<sample::Position>());
    static_assert(typeId<sample::Position>() != typeId<sample::Velocity>());
    EXPECT_NE(typeId<sample::Wrapper<int>>(), typeId<sample::Wrapper<float>>());
    EXPECT_EQ(typeId<sample::Position>(), fnv1a("sample::Position"));
}
