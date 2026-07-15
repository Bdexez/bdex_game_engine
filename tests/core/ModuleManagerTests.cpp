#include "plugins/TestService.hpp"

#include <bdex/core/Engine.hpp>
#include <bdex/core/FileSystem.hpp>
#include <bdex/core/ModuleManager.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace bdex;

namespace {

ModuleInfo makeInfo(std::string name, std::vector<std::string> provides = {},
                    std::vector<std::string> dependencies = {},
                    std::vector<std::string> after = {}) {
    ModuleInfo info;
    info.name = std::move(name);
    info.provides = std::move(provides);
    info.dependencies = std::move(dependencies);
    info.after = std::move(after);
    return info;
}

std::vector<std::string> names(const std::vector<ModuleInfo>& infos,
                               const std::vector<usize>& order) {
    std::vector<std::string> result;
    for (const usize index : order) {
        result.push_back(infos[index].name);
    }
    return result;
}

/// Module defined by the test itself, recording its lifecycle.
class RecordingModule final : public IModule {
public:
    RecordingModule(ModuleInfo info, std::vector<std::string>& trace, bool failInit = false)
        : info_(std::move(info)), trace_(&trace), failInit_(failInit) {}

    [[nodiscard]] ModuleInfo info() const override { return info_; }

    Result<> onInit(Engine&) override {
        if (failInit_) {
            return fail("boom");
        }
        trace_->push_back("init:" + info_.name);
        return {};
    }

    void onShutdown(Engine&) override { trace_->push_back("shutdown:" + info_.name); }

private:
    ModuleInfo info_;
    std::vector<std::string>* trace_;
    bool failInit_;
};

}  // namespace

// --- Dependency resolution ---------------------------------------------------

TEST(ModuleOrder, DependenciesComeFirstRegardlessOfLoadOrder) {
    const std::vector<ModuleInfo> infos = {
        makeInfo("renderer", {}, {"rhi"}, {"window"}),
        makeInfo("rhi_vulkan", {"rhi"}, {}, {"window"}),
        makeInfo("window_glfw", {"window"}),
    };
    const auto order = ModuleManager::resolveOrder(infos);
    ASSERT_TRUE(order.has_value()) << order.error().message;
    EXPECT_EQ(names(infos, *order),
              (std::vector<std::string>{"window_glfw", "rhi_vulkan", "renderer"}));
}

TEST(ModuleOrder, IndependentModulesKeepLoadOrder) {
    const std::vector<ModuleInfo> infos = {makeInfo("c"), makeInfo("a"), makeInfo("b")};
    const auto order = ModuleManager::resolveOrder(infos);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(names(infos, *order), (std::vector<std::string>{"c", "a", "b"}));
}

TEST(ModuleOrder, OptionalOrderingIsIgnoredWhenCapabilityIsAbsent) {
    const std::vector<ModuleInfo> infos = {makeInfo("rhi_vulkan", {"rhi"}, {}, {"window"})};
    const auto order = ModuleManager::resolveOrder(infos);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->size(), 1u);
}

TEST(ModuleOrder, MissingDependencyIsReported) {
    const std::vector<ModuleInfo> infos = {makeInfo("renderer", {}, {"rhi"})};
    const auto order = ModuleManager::resolveOrder(infos);
    ASSERT_FALSE(order.has_value());
    EXPECT_NE(order.error().message.find("'rhi'"), std::string::npos);
}

TEST(ModuleOrder, CycleIsReported) {
    const std::vector<ModuleInfo> infos = {
        makeInfo("a", {}, {"b"}),
        makeInfo("b", {}, {"a"}),
        makeInfo("free"),
    };
    const auto order = ModuleManager::resolveOrder(infos);
    ASSERT_FALSE(order.has_value());
    EXPECT_NE(order.error().message.find("cyclic"), std::string::npos);
}

// --- Lifecycle ----------------------------------------------------------------

TEST(ModuleManager, InitializesInDependencyOrderAndShutsDownInReverse) {
    std::vector<std::string> trace;
    Engine engine;
    ASSERT_TRUE(engine.modules()
                    .add(std::make_unique<RecordingModule>(makeInfo("game", {}, {"physics"}), trace))
                    .has_value());
    ASSERT_TRUE(
        engine.modules().add(std::make_unique<RecordingModule>(makeInfo("physics"), trace)).has_value());

    ASSERT_TRUE(engine.initialize().has_value());
    EXPECT_TRUE(engine.modules().isInitialized("game"));
    EXPECT_EQ(engine.modules().initializationOrder(), (std::vector<std::string>{"physics", "game"}));

    engine.shutdown();
    EXPECT_EQ(trace, (std::vector<std::string>{"init:physics", "init:game", "shutdown:game",
                                               "shutdown:physics"}));
}

TEST(ModuleManager, FailedInitRollsBackAlreadyInitializedModules) {
    std::vector<std::string> trace;
    Engine engine;
    ASSERT_TRUE(
        engine.modules().add(std::make_unique<RecordingModule>(makeInfo("base"), trace)).has_value());
    ASSERT_TRUE(engine.modules()
                    .add(std::make_unique<RecordingModule>(makeInfo("broken", {}, {"base"}), trace, true))
                    .has_value());

    const auto result = engine.initialize();
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("broken"), std::string::npos);
    EXPECT_FALSE(engine.modules().isInitialized("base"));
    EXPECT_EQ(trace, (std::vector<std::string>{"init:base", "shutdown:base"}));
}

TEST(ModuleManager, RejectsDuplicatesAndUnknownModules) {
    std::vector<std::string> trace;
    Engine engine;
    ASSERT_TRUE(
        engine.modules().add(std::make_unique<RecordingModule>(makeInfo("dup"), trace)).has_value());
    EXPECT_FALSE(
        engine.modules().add(std::make_unique<RecordingModule>(makeInfo("dup"), trace)).has_value());

    const auto missing = engine.modules().load("does_not_exist");
    ASSERT_FALSE(missing.has_value());
    EXPECT_NE(missing.error().message.find("does_not_exist"), std::string::npos);
}

// --- Loading by name (plugin in shared builds, static registry otherwise) -----

TEST(ModuleManager, LoadsModuleByNameAndTalksThroughInterfacesOnly) {
    Engine engine;
    engine.modules().addSearchPath(fs::executableDirectory());

    const auto loaded = engine.modules().load("test_plugin");
    ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
    EXPECT_TRUE(engine.modules().load("test_plugin").has_value());  // idempotent
    ASSERT_TRUE(engine.initialize().has_value());

    // Service implemented inside the module, resolved by interface type id.
    auto* service = engine.services().tryGet<tests::ITestService>();
    ASSERT_NE(service, nullptr);
    EXPECT_EQ(service->answer(), 42);

    // Events cross the module boundary in both directions.
    int pong = 0;
    const Subscription subscription = engine.events().subscribe<tests::TestPong>(
        [&](const tests::TestPong& event) { pong = event.value; });
    engine.events().publish(tests::TestPing{21});
    EXPECT_EQ(service->pingsReceived(), 1);
    EXPECT_EQ(pong, 42);

    // Tasks scheduled by the module run as part of the frame.
    engine.tick();
    engine.tick();
    EXPECT_EQ(service->updatesRun(), 2);

    engine.shutdown();
    EXPECT_EQ(engine.services().tryGet<tests::ITestService>(), nullptr);
}

TEST(ModuleManager, ModuleInitFailureIsPropagated) {
    Config config;
    config.set("test_plugin.fail_init", "true");
    Engine engine(config);
    engine.modules().addSearchPath(fs::executableDirectory());
    ASSERT_TRUE(engine.modules().load("test_plugin").has_value());

    const auto result = engine.initialize();
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("failure requested"), std::string::npos);
}
