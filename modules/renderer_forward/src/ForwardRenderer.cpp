#include "ForwardRenderer.hpp"

#include "MeshFeature.hpp"

#include <bdex/assets/Image.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/scene/Scene.hpp>

#include <algorithm>
#include <array>

namespace bdex::forward {

ForwardRenderer::ForwardRenderer(Engine& engine, rhi::IDevice& device)
    : engine_(engine), device_(device) {}

ForwardRenderer::~ForwardRenderer() {
    shutdown();
}

// =============================================================================
// Lifecycle
// =============================================================================

Result<> ForwardRenderer::initialize() {
    const Config& config = engine_.config();
    fallbackExtent_ = {
        static_cast<u32>(std::clamp<i64>(config.getInt("renderer.width", 1280), 1, 16384)),
        static_cast<u32>(std::clamp<i64>(config.getInt("renderer.height", 720), 1, 16384)),
    };

    // --- Output: a swapchain when there is a real window, offscreen otherwise --
    auto* windows = engine_.services().tryGet<platform::IWindowSystem>();
    window_ = windows != nullptr ? windows->primaryWindow() : nullptr;
    if (window_ != nullptr && !windows->isHeadless()) {
        const auto swapchain =
            device_.createSwapchain({window_, config.getBool("renderer.vsync", true)});
        if (!swapchain) {
            return fail("cannot create the swapchain: {}", swapchain.error().message);
        }
        swapchain_ = *swapchain;
        colorFormat_ = device_.swapchainFormat(swapchain_);
    } else {
        log::info("renderer", "no presentable window: rendering offscreen");
    }
    initialized_ = true;

    // --- Frame constants (set 0) ---------------------------------------------------
    frameLayout_ = device_.createBindGroupLayout({
        {{0, rhi::BindingType::UniformBuffer,
          rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment}},
        "frame layout",
    });
    for (u32 slot = 0; slot < device_.framesInFlight(); ++slot) {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(render::FrameUniforms);
        bufferDesc.usage = rhi::BufferUsage::Uniform;
        bufferDesc.memory = rhi::MemoryUsage::CpuToGpu;
        bufferDesc.debugName = "frame uniforms";
        frameBuffers_.push_back(device_.createBuffer(bufferDesc));

        rhi::BindGroupDesc groupDesc;
        groupDesc.layout = frameLayout_;
        groupDesc.entries.push_back({.binding = 0, .buffer = frameBuffers_.back()});
        groupDesc.debugName = "frame bind group";
        frameBindGroups_.push_back(device_.createBindGroup(groupDesc));
    }

    // --- Material resources (set 1) --------------------------------------------------
    materialLayout_ = device_.createBindGroupLayout({
        {{0, rhi::BindingType::CombinedTextureSampler, rhi::ShaderStage::Fragment}},
        "material layout",
    });
    rhi::SamplerDesc samplerDesc;
    samplerDesc.debugName = "default sampler";
    defaultSampler_ = device_.createSampler(samplerDesc);
    whiteTexture_ = createTexture(assets::makeSolidImage(1, 1, {255, 255, 255, 255}));
    defaultMaterial_ = createMaterial({});

    // --- Built-in features -------------------------------------------------------------
    return addFeature(std::make_unique<MeshFeature>(*this));
}

void ForwardRenderer::shutdown() {
    if (!initialized_) {
        return;
    }
    initialized_ = false;
    device_.waitIdle();

    for (auto& feature : features_) {
        feature->onDetach(device_);
    }
    features_.clear();

    materials_.forEach([this](render::MaterialHandle, GpuMaterial& material) {
        device_.destroyBindGroup(material.bindGroup);
    });
    materials_.clear();
    textures_.forEach([this](render::TextureHandle, rhi::TextureHandle& texture) {
        device_.destroyTexture(texture);
    });
    textures_.clear();
    meshes_.forEach([this](render::MeshHandle, GpuMesh& mesh) {
        device_.destroyBuffer(mesh.vertexBuffer);
        device_.destroyBuffer(mesh.indexBuffer);
    });
    meshes_.clear();

    for (const rhi::BindGroupHandle group : frameBindGroups_) {
        device_.destroyBindGroup(group);
    }
    for (const rhi::BufferHandle buffer : frameBuffers_) {
        device_.destroyBuffer(buffer);
    }
    frameBindGroups_.clear();
    frameBuffers_.clear();

    device_.destroySampler(defaultSampler_);
    device_.destroyBindGroupLayout(materialLayout_);
    device_.destroyBindGroupLayout(frameLayout_);
    device_.destroyTexture(depthTarget_);
    device_.destroyTexture(captureTarget_);
    device_.destroySwapchain(swapchain_);
    swapchain_ = {};
}

render::RenderSetupContext ForwardRenderer::setupContext() {
    return render::RenderSetupContext{device_, engine_.services(), colorFormat_, depthFormat_,
                                      frameLayout_};
}

// =============================================================================
// Resources
// =============================================================================

render::MeshHandle ForwardRenderer::createMesh(const assets::MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        log::warn("renderer", "createMesh called with an empty mesh");
        return {};
    }

    GpuMesh gpuMesh;
    gpuMesh.indexCount = static_cast<u32>(mesh.indices.size());

    rhi::BufferDesc vertexDesc;
    vertexDesc.size = mesh.vertices.size() * sizeof(assets::Vertex);
    vertexDesc.usage = rhi::BufferUsage::Vertex;
    vertexDesc.debugName = "mesh vertices";
    gpuMesh.vertexBuffer = device_.createBuffer(vertexDesc);
    device_.writeBuffer(gpuMesh.vertexBuffer, rhi::asBytes(mesh.vertices));

    rhi::BufferDesc indexDesc;
    indexDesc.size = mesh.indices.size() * sizeof(u32);
    indexDesc.usage = rhi::BufferUsage::Index;
    indexDesc.debugName = "mesh indices";
    gpuMesh.indexBuffer = device_.createBuffer(indexDesc);
    device_.writeBuffer(gpuMesh.indexBuffer, rhi::asBytes(mesh.indices));

    return meshes_.insert(gpuMesh);
}

void ForwardRenderer::destroyMesh(render::MeshHandle handle) {
    if (const auto mesh = meshes_.take(handle)) {
        device_.destroyBuffer(mesh->vertexBuffer);
        device_.destroyBuffer(mesh->indexBuffer);
    }
}

render::TextureHandle ForwardRenderer::createTexture(const assets::ImageData& image) {
    if (!image.valid()) {
        log::warn("renderer", "createTexture called with an invalid image");
        return {};
    }
    rhi::TextureDesc desc;
    desc.width = image.width;
    desc.height = image.height;
    desc.format = rhi::Format::RGBA8Srgb;  // color data, authored in sRGB
    desc.usage = rhi::TextureUsage::Sampled;
    desc.debugName = "texture";
    const rhi::TextureHandle texture = device_.createTexture(desc);
    device_.writeTexture(texture, rhi::asBytes(image.pixels));
    return textures_.insert(texture);
}

void ForwardRenderer::destroyTexture(render::TextureHandle handle) {
    if (const auto texture = textures_.take(handle)) {
        device_.destroyTexture(*texture);
    }
}

render::MaterialHandle ForwardRenderer::createMaterial(const render::MaterialDesc& desc) {
    const rhi::TextureHandle* albedo = textures_.get(desc.albedo);
    if (albedo == nullptr) {
        albedo = textures_.get(whiteTexture_);
    }

    rhi::BindGroupDesc groupDesc;
    groupDesc.layout = materialLayout_;
    groupDesc.entries.push_back({.binding = 0, .texture = *albedo, .sampler = defaultSampler_});
    groupDesc.debugName = "material";

    GpuMaterial material;
    material.desc = desc;
    material.bindGroup = device_.createBindGroup(groupDesc);
    return materials_.insert(material);
}

void ForwardRenderer::destroyMaterial(render::MaterialHandle handle) {
    if (handle == defaultMaterial_) {
        return;
    }
    if (const auto material = materials_.take(handle)) {
        device_.destroyBindGroup(material->bindGroup);
    }
}

const GpuMaterial& ForwardRenderer::materialOrDefault(render::MaterialHandle handle) const {
    if (const GpuMaterial* material = materials_.get(handle)) {
        return *material;
    }
    return *materials_.get(defaultMaterial_);
}

Result<> ForwardRenderer::addFeature(std::unique_ptr<render::IRenderFeature> feature) {
    if (feature == nullptr) {
        return fail("cannot add a null render feature");
    }
    const std::string_view name = feature->name();
    const bool duplicate = std::any_of(features_.begin(), features_.end(),
                                       [name](const auto& f) { return f->name() == name; });
    if (duplicate) {
        return fail("a render feature named '{}' is already attached", name);
    }
    if (const auto attached = feature->onAttach(setupContext()); !attached) {
        return fail("render feature '{}' failed to attach: {}", name, attached.error().message);
    }

    // Keep features sorted by order; equal orders keep insertion order.
    const auto position = std::upper_bound(
        features_.begin(), features_.end(), feature->order(),
        [](i32 order, const auto& existing) { return order < existing->order(); });
    features_.insert(position, std::move(feature));
    return {};
}

bool ForwardRenderer::removeFeature(std::string_view name) {
    const auto it = std::find_if(features_.begin(), features_.end(),
                                 [name](const auto& f) { return f->name() == name; });
    if (it == features_.end()) {
        return false;
    }
    (*it)->onDetach(device_);
    features_.erase(it);
    return true;
}

// =============================================================================
// Frame
// =============================================================================

Extent2D ForwardRenderer::targetExtent() const {
    return window_ != nullptr ? window_->framebufferSize() : fallbackExtent_;
}

void ForwardRenderer::ensureDepthTarget(Extent2D extent) {
    if (depthTarget_.valid() && depthExtent_ == extent) {
        return;
    }
    device_.destroyTexture(depthTarget_);  // release is deferred by the device
    rhi::TextureDesc desc;
    desc.width = extent.width;
    desc.height = extent.height;
    desc.format = depthFormat_;
    desc.usage = rhi::TextureUsage::DepthStencilAttachment;
    desc.debugName = "depth target";
    depthTarget_ = device_.createTexture(desc);
    depthExtent_ = extent;
}

void ForwardRenderer::ensureCaptureTarget(Extent2D extent) {
    if (captureTarget_.valid() && captureExtent_ == extent) {
        return;
    }
    device_.destroyTexture(captureTarget_);
    rhi::TextureDesc desc;
    desc.width = extent.width;
    desc.height = extent.height;
    // Same format as the swapchain so features need a single pipeline variant.
    desc.format = colorFormat_;
    desc.usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::TransferSrc;
    desc.debugName = "capture target";
    captureTarget_ = device_.createTexture(desc);
    captureExtent_ = extent;
}

void ForwardRenderer::updateFrameUniforms(const render::CameraData& camera, Extent2D extent,
                                          const FrameTime& time) {
    render::DirectionalLight light;  // defaults when the scene has none
    if (auto* scenes = engine_.services().tryGet<scene::SceneManager>();
        scenes != nullptr && scenes->active() != nullptr) {
        scenes->active()->world().view<render::DirectionalLight>().each(
            [&light](const render::DirectionalLight& candidate) { light = candidate; });
    }

    render::FrameUniforms uniforms;
    uniforms.view = camera.view;
    uniforms.projection = camera.projection;
    uniforms.viewProjection = camera.viewProjection;
    uniforms.inverseViewProjection = glm::inverse(camera.viewProjection);
    uniforms.cameraPosition = Vec4(camera.position, 1.0f);
    const f32 directionLength = glm::length(light.direction);
    uniforms.lightDirection = Vec4(
        directionLength > 0.0f ? light.direction / directionLength : Vec3(0.0f, -1.0f, 0.0f), 0.0f);
    uniforms.lightColor = Vec4(light.color * light.intensity, 1.0f);
    uniforms.ambientColor = Vec4(light.ambient, 1.0f);
    uniforms.timeAndExtent =
        Vec4(static_cast<f32>(time.elapsed), static_cast<f32>(time.delta),
             static_cast<f32>(extent.width), static_cast<f32>(extent.height));

    device_.writeBuffer(frameBuffers_[device_.frameSlot()], rhi::asBytes(uniforms));
}

void ForwardRenderer::renderView(rhi::ICommandList& commands, rhi::TextureHandle target,
                                 Extent2D extent, const render::CameraData& camera,
                                 const FrameTime& time) {
    const std::array<rhi::ColorAttachment, 1> colorAttachments = {{
        {target, rhi::LoadOp::Clear, rhi::StoreOp::Store,
         {clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a}},
    }};
    const rhi::DepthAttachment depthAttachment{depthTarget_, rhi::LoadOp::Clear,
                                               rhi::StoreOp::DontCare, 1.0f};

    commands.beginRendering({colorAttachments, &depthAttachment});
    commands.setViewport({0.0f, 0.0f, static_cast<f32>(extent.width),
                          static_cast<f32>(extent.height), 0.0f, 1.0f});
    commands.setScissor({0, 0, extent.width, extent.height});

    auto* scenes = engine_.services().tryGet<scene::SceneManager>();
    scene::Scene* activeScene = scenes != nullptr ? scenes->active() : nullptr;
    const render::RenderFrameContext context{
        device_,
        commands,
        activeScene != nullptr ? &activeScene->world() : nullptr,
        camera,
        extent,
        frameBindGroups_[device_.frameSlot()],
        time,
    };
    for (const auto& feature : features_) {
        feature->render(context);
    }
    commands.endRendering();
}

void ForwardRenderer::renderFrame(const FrameTime& time) {
    rhi::ICommandList& commands = device_.beginFrame();

    // Where this frame goes: the window's backbuffer, or the offscreen target.
    rhi::TextureHandle target;
    if (swapchain_.valid()) {
        target = device_.acquireSwapchainTexture(swapchain_);
    } else if (const Extent2D size = targetExtent(); size.width > 0 && size.height > 0) {
        ensureCaptureTarget(size);
        target = captureTarget_;
    }

    const bool capture = !screenshotPath_.empty();
    bool captured = false;
    if (target.valid()) {
        const rhi::TextureDesc targetDesc = device_.textureDesc(target);
        extent_ = {targetDesc.width, targetDesc.height};
        ensureDepthTarget(extent_);

        // Camera: the scene's primary camera, or a default vantage point.
        const f32 aspect = static_cast<f32>(extent_.width) / static_cast<f32>(extent_.height);
        render::CameraData camera = render::makeCameraData(
            glm::inverse(glm::lookAt(Vec3(0.0f, 2.0f, 6.0f), Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f))),
            render::Camera{}, aspect);
        if (auto* scenes = engine_.services().tryGet<scene::SceneManager>();
            scenes != nullptr && scenes->active() != nullptr) {
            bool found = false;
            scenes->active()->world().view<render::Camera, scene::WorldTransform>().each(
                [&](const render::Camera& candidate, const scene::WorldTransform& transform) {
                    if (!found && candidate.primary) {
                        camera = render::makeCameraData(transform.matrix, candidate, aspect);
                        found = true;
                    }
                });
        }

        updateFrameUniforms(camera, extent_, time);
        renderView(commands, target, extent_, camera, time);

        if (capture) {
            // Swapchain images cannot be read back once presented: when
            // rendering to a window, the frame is drawn a second time into
            // the capture target.
            if (swapchain_.valid()) {
                ensureCaptureTarget(extent_);
                renderView(commands, captureTarget_, extent_, camera, time);
            }
            captured = true;
        }
    }

    device_.endFrame();

    if (captured) {
        saveCapture(screenshotPath_);
        screenshotPath_.clear();
    }
}

void ForwardRenderer::saveCapture(const std::filesystem::path& path) {
    assets::ImageData image;
    image.width = captureExtent_.width;
    image.height = captureExtent_.height;
    image.pixels = device_.readTexture(captureTarget_);  // waits for the GPU

    const auto written = assets::writePng(path, image);
    if (written) {
        log::info("renderer", "screenshot saved to {}", path.string());
    } else {
        log::error("renderer", "screenshot failed: {}", written.error().message);
    }
    engine_.events().publish(render::ScreenshotSaved{path, written.has_value()});
}

}  // namespace bdex::forward
