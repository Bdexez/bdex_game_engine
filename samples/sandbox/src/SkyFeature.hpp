#pragma once

#include <bdex/render/Renderer.hpp>

namespace sandbox {

/// A procedural sky drawn behind the scene.
///
/// This is application code extending the engine's renderer: it uses nothing
/// but the public RHI and render interfaces, has its own shaders and pipeline,
/// and is attached with IRenderer::addFeature(). The renderer module is not
/// modified - and does not even know this feature exists.
class SkyFeature final : public bdex::render::IRenderFeature {
public:
    [[nodiscard]] std::string_view name() const override { return "sandbox.sky"; }
    /// Before scene geometry (order 0).
    [[nodiscard]] bdex::i32 order() const override { return -100; }

    bdex::Result<> onAttach(const bdex::render::RenderSetupContext& context) override;
    void onDetach(bdex::rhi::IDevice& device) override;
    void render(const bdex::render::RenderFrameContext& context) override;

private:
    bdex::rhi::PipelineHandle pipeline_;
};

}  // namespace sandbox
