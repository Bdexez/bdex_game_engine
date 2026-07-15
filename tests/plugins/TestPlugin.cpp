#include "plugins/TestService.hpp"

#include <bdex/core/Engine.hpp>
#include <bdex/core/Module.hpp>

#include <memory>

namespace bdex::tests {
namespace {

class TestService final : public ITestService {
public:
    [[nodiscard]] int answer() const override { return 42; }
    [[nodiscard]] int pingsReceived() const override { return pings; }
    [[nodiscard]] int updatesRun() const override { return updates; }

    int pings = 0;
    int updates = 0;
};

class TestPluginModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "Module used by the engine tests",
            .provides = {"test_capability"},
            .dependencies = {},
            .after = {},
        };
    }

    Result<> onInit(Engine& engine) override {
        if (engine.config().getBool("test_plugin.fail_init", false)) {
            return fail("failure requested by configuration");
        }

        service_ = std::make_shared<TestService>();
        engine.services().provide<ITestService>(service_);

        // Answer every ping with a pong carrying twice the value.
        pingSubscription_ = engine.events().subscribe<TestPing>(
            [this, &engine](const TestPing& ping) {
                ++service_->pings;
                engine.events().publish(TestPong{ping.value * 2});
            });

        updateTask_ = ScopedTask(engine.scheduler(),
                                 engine.scheduler().add(Phase::Update, "test_plugin.update",
                                                        [this](const FrameTime&) {
                                                            ++service_->updates;
                                                        }));
        return {};
    }

    void onShutdown(Engine& engine) override {
        updateTask_.reset();
        pingSubscription_.reset();
        engine.services().remove<ITestService>();
        service_.reset();
    }

private:
    std::shared_ptr<TestService> service_;
    Subscription pingSubscription_;
    ScopedTask updateTask_;
};

}  // namespace
}  // namespace bdex::tests

BDEX_DEFINE_MODULE(bdex::tests::TestPluginModule)
