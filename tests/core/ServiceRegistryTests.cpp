#include <bdex/core/ServiceRegistry.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace bdex;

namespace {

class IGreeter {
public:
    virtual ~IGreeter() = default;
    [[nodiscard]] virtual std::string greet() const = 0;
};

class English final : public IGreeter {
public:
    [[nodiscard]] std::string greet() const override { return "hello"; }
};

class French final : public IGreeter {
public:
    [[nodiscard]] std::string greet() const override { return "bonjour"; }
};

struct Tracker {
    Tracker(std::vector<int>* destroyed, int identifier) : log(destroyed), id(identifier) {}
    Tracker(const Tracker&) = delete;
    ~Tracker() { log->push_back(id); }

    std::vector<int>* log;
    int id;
};
struct First : Tracker {
    using Tracker::Tracker;
};
struct Second : Tracker {
    using Tracker::Tracker;
};

}  // namespace

TEST(ServiceRegistry, ResolvesByInterface) {
    ServiceRegistry services;
    EXPECT_EQ(services.tryGet<IGreeter>(), nullptr);
    EXPECT_FALSE(services.has<IGreeter>());

    services.provide<IGreeter>(std::make_shared<English>());
    ASSERT_TRUE(services.has<IGreeter>());
    EXPECT_EQ(services.get<IGreeter>().greet(), "hello");
}

TEST(ServiceRegistry, LaterProviderReplacesEarlierOne) {
    ServiceRegistry services;
    services.provide<IGreeter>(std::make_shared<English>());
    services.provide<IGreeter>(std::make_shared<French>());
    EXPECT_EQ(services.get<IGreeter>().greet(), "bonjour");
    EXPECT_EQ(services.names().size(), 1u);
}

TEST(ServiceRegistry, ProvideRefDoesNotOwn) {
    English english;
    {
        ServiceRegistry services;
        services.provideRef<IGreeter>(english);
        EXPECT_EQ(&services.get<IGreeter>(), &english);
    }
    EXPECT_EQ(english.greet(), "hello");  // still alive
}

TEST(ServiceRegistry, RemoveAndReverseOrderDestruction) {
    std::vector<int> destroyed;
    {
        ServiceRegistry services;
        services.provide<First>(std::make_shared<First>(&destroyed, 1));
        services.provide<Second>(std::make_shared<Second>(&destroyed, 2));
        EXPECT_TRUE(services.has<First>());
        EXPECT_TRUE(services.has<Second>());
    }
    // Services registered last are destroyed first.
    EXPECT_EQ(destroyed, (std::vector<int>{2, 1}));

    ServiceRegistry services;
    services.provide<IGreeter>(std::make_shared<English>());
    services.remove<IGreeter>();
    EXPECT_FALSE(services.has<IGreeter>());
}
