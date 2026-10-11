// renderer_forward: forward renderer built on the RHI.
//
// Requires any module providing the "rhi" capability; uses the window system
// when one is loaded and renders offscreen otherwise.
//
// Configuration:
//   renderer.vsync                    (default true)
//   renderer.width, renderer.height   offscreen size when there is no window
//                                     system at all (default 1280x720)

#include "ForwardRenderer.hpp"

#include <bdex/core/Module.hpp>

namespace bdex {
namespace {

class ForwardRendererModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "Forward renderer",
            .provides = {"renderer"},
            .dependencies = {"rhi"},
            .after = {"window"},
        };
    }

    Result<> onInit(Engine& engine) override {
        auto& device = engine.services().get<rhi::IDevice>();
        renderer_ = std::make_shared<forward::ForwardRenderer>(engine, device);
        if (const auto initialized = renderer_->initialize(); !initialized) {
            renderer_.reset();
            return initialized;
        }
        engine.services().provide<render::IRenderer>(renderer_);

        renderTask_ = ScopedTask(
            engine.scheduler(),
            engine.scheduler().add(Phase::Render, "renderer_forward.render",
                                   [renderer = renderer_.get()](const FrameTime& time) {
                                       renderer->renderFrame(time);
                                   }));
        return {};
    }

    void onShutdown(Engine& engine) override {
        renderTask_.reset();
        engine.services().remove<render::IRenderer>();
        if (renderer_ != nullptr) {
            // Explicit: the renderer must release its GPU objects while the
            // RHI module (shut down after us) is still alive.
            renderer_->shutdown();
            renderer_.reset();
        }
    }

private:
    std::shared_ptr<forward::ForwardRenderer> renderer_;
    ScopedTask renderTask_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::ForwardRendererModule)
