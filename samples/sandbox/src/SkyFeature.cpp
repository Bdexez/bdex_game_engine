#include "SkyFeature.hpp"

#include <bdex/assets/AssetManager.hpp>

namespace sandbox {

using namespace bdex;

namespace {

/// Must match SkyConstants in sky.frag.
struct SkyConstants {
    Vec4 horizonColor{0.62f, 0.74f, 0.88f, 1.0f};
    Vec4 zenithColor{0.10f, 0.26f, 0.58f, 1.0f};
    Vec4 groundColor{0.16f, 0.15f, 0.14f, 1.0f};
};

}  // namespace

Result<> SkyFeature::onAttach(const render::RenderSetupContext& context) {
    auto& assets = context.services.get<assets::AssetManager>();
    const auto vertexBinary = assets.load<assets::ShaderBinary>("app:/shaders/sky.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("app:/shaders/sky.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }

    rhi::IDevice& device = context.device;
    const rhi::ShaderHandle vertexShader =
        device.createShader({rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "sky.vert"});
    const rhi::ShaderHandle fragmentShader = device.createShader(
        {rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "sky.frag"});

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    // No vertex layout: the triangle comes from gl_VertexIndex.
    desc.bindGroupLayouts = {context.frameLayout};
    desc.pushConstantSize = sizeof(SkyConstants);
    desc.pushConstantStages = rhi::ShaderStage::Fragment;
    desc.colorFormats = {context.colorFormat};
    desc.depthFormat = context.depthFormat;
    desc.cullMode = rhi::CullMode::None;
    // Drawn first, on the far plane, without touching depth: geometry simply
    // draws over it.
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.debugName = "sandbox.sky";
    pipeline_ = device.createGraphicsPipeline(desc);

    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    return {};
}

void SkyFeature::onDetach(rhi::IDevice& device) {
    device.destroyPipeline(pipeline_);
    pipeline_ = {};
}

void SkyFeature::render(const render::RenderFrameContext& context) {
    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("sandbox.sky");
    commands.bindPipeline(pipeline_);
    commands.bindBindGroup(0, context.frameBindGroup);
    commands.pushConstants(0, rhi::asBytes(SkyConstants{}));
    commands.draw(3);
    commands.endDebugGroup();
}

}  // namespace sandbox
