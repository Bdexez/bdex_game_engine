#include "MeshFeature.hpp"

#include "ForwardRenderer.hpp"

#include <bdex/assets/AssetManager.hpp>
#include <bdex/scene/Components.hpp>

#include <cstddef>

namespace bdex::forward {
namespace {

/// Must match ObjectConstants in mesh.vert / mesh.frag.
struct ObjectConstants {
    Mat4 model;
    Vec4 baseColor;
    Vec4 params;  // x: specular strength, y: shininess
};

}  // namespace

Result<> MeshFeature::onAttach(const render::RenderSetupContext& context) {
    auto& assets = context.services.get<assets::AssetManager>();
    const auto vertexBinary = assets.load<assets::ShaderBinary>("engine:/shaders/mesh.vert.spv");
    if (!vertexBinary) {
        return std::unexpected(vertexBinary.error());
    }
    const auto fragmentBinary = assets.load<assets::ShaderBinary>("engine:/shaders/mesh.frag.spv");
    if (!fragmentBinary) {
        return std::unexpected(fragmentBinary.error());
    }

    rhi::IDevice& device = context.device;
    const rhi::ShaderHandle vertexShader =
        device.createShader({rhi::ShaderStage::Vertex, (*vertexBinary)->words, "main", "mesh.vert"});
    const rhi::ShaderHandle fragmentShader = device.createShader(
        {rhi::ShaderStage::Fragment, (*fragmentBinary)->words, "main", "mesh.frag"});

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader;
    desc.fragmentShader = fragmentShader;
    desc.vertexLayout.stride = sizeof(assets::Vertex);
    desc.vertexLayout.attributes = {
        {0, rhi::VertexFormat::Float3, offsetof(assets::Vertex, position)},
        {1, rhi::VertexFormat::Float3, offsetof(assets::Vertex, normal)},
        {2, rhi::VertexFormat::Float2, offsetof(assets::Vertex, uv)},
    };
    desc.bindGroupLayouts = {context.frameLayout, renderer_->materialLayout()};
    desc.pushConstantSize = sizeof(ObjectConstants);
    desc.pushConstantStages = rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment;
    desc.colorFormats = {context.colorFormat};
    desc.depthFormat = context.depthFormat;
    desc.cullMode = rhi::CullMode::Back;
    desc.frontFace = rhi::FrontFace::CounterClockwise;
    desc.debugName = "forward.meshes";
    pipeline_ = device.createGraphicsPipeline(desc);

    // The pipeline keeps what it needs from the shader modules.
    device.destroyShader(vertexShader);
    device.destroyShader(fragmentShader);
    return {};
}

void MeshFeature::onDetach(rhi::IDevice& device) {
    device.destroyPipeline(pipeline_);
    pipeline_ = {};
}

void MeshFeature::render(const render::RenderFrameContext& context) {
    if (context.world == nullptr) {
        return;
    }

    rhi::ICommandList& commands = context.commands;
    commands.beginDebugGroup("forward.meshes");
    commands.bindPipeline(pipeline_);
    commands.bindBindGroup(0, context.frameBindGroup);

    // Consecutive draws often share a material or a mesh: skip redundant binds.
    rhi::BindGroupHandle boundMaterial;
    const GpuMesh* boundMesh = nullptr;

    context.world->view<render::MeshRenderer, scene::WorldTransform>().each(
        [&](const render::MeshRenderer& renderable, const scene::WorldTransform& transform) {
            if (!renderable.visible) {
                return;
            }
            const GpuMesh* mesh = renderer_->mesh(renderable.mesh);
            if (mesh == nullptr) {
                return;  // destroyed or never assigned
            }
            const GpuMaterial& material = renderer_->materialOrDefault(renderable.material);

            if (material.bindGroup != boundMaterial) {
                commands.bindBindGroup(1, material.bindGroup);
                boundMaterial = material.bindGroup;
            }
            if (mesh != boundMesh) {
                commands.bindVertexBuffer(mesh->vertexBuffer);
                commands.bindIndexBuffer(mesh->indexBuffer, rhi::IndexType::UInt32);
                boundMesh = mesh;
            }

            const ObjectConstants constants{
                transform.matrix,
                material.desc.baseColor,
                Vec4(material.desc.specular, material.desc.shininess, 0.0f, 0.0f),
            };
            commands.pushConstants(0, rhi::asBytes(constants));
            commands.drawIndexed(mesh->indexCount);
        });

    commands.endDebugGroup();
}

}  // namespace bdex::forward
