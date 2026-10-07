#pragma once

#include <bdex/render/Renderer.hpp>

namespace bdex::forward {

class ForwardRenderer;

/// Built-in feature drawing every entity with a MeshRenderer and a
/// WorldTransform, lit by the scene's directional light (Blinn-Phong).
class MeshFeature final : public render::IRenderFeature {
public:
    explicit MeshFeature(const ForwardRenderer& renderer) : renderer_(&renderer) {}

    [[nodiscard]] std::string_view name() const override { return "forward.meshes"; }
    [[nodiscard]] i32 order() const override { return 0; }

    Result<> onAttach(const render::RenderSetupContext& context) override;
    void onDetach(rhi::IDevice& device) override;
    void render(const render::RenderFrameContext& context) override;

private:
    const ForwardRenderer* renderer_;
    rhi::PipelineHandle pipeline_;
};

}  // namespace bdex::forward
