#pragma once

#include <bdex/core/Engine.hpp>
#include <bdex/core/Handle.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/render/Renderer.hpp>
#include <bdex/rhi/Device.hpp>

#include <filesystem>
#include <memory>
#include <vector>

namespace bdex::forward {

struct GpuMesh {
    rhi::BufferHandle vertexBuffer;
    rhi::BufferHandle indexBuffer;
    u32 indexCount = 0;
};

struct GpuMaterial {
    render::MaterialDesc desc;
    rhi::BindGroupHandle bindGroup;  ///< Set 1: albedo texture + sampler.
};

/// Single-pass forward renderer written purely against the RHI: it runs
/// unchanged on any backend module, with or without a window.
///
/// Frame: one color + depth pass in which every render feature draws, in
/// order. The renderer owns the frame constants (set 0) and the mesh,
/// texture and material resources; what gets drawn is decided by features.
class ForwardRenderer final : public render::IRenderer {
public:
    ForwardRenderer(Engine& engine, rhi::IDevice& device);
    ~ForwardRenderer() override;

    Result<> initialize();
    void shutdown();
    void renderFrame(const FrameTime& time);

    // --- render::IRenderer -----------------------------------------------------
    [[nodiscard]] render::MeshHandle createMesh(const assets::MeshData& mesh) override;
    void destroyMesh(render::MeshHandle mesh) override;
    [[nodiscard]] render::TextureHandle createTexture(const assets::ImageData& image) override;
    void destroyTexture(render::TextureHandle texture) override;
    [[nodiscard]] render::MaterialHandle createMaterial(const render::MaterialDesc& desc) override;
    void destroyMaterial(render::MaterialHandle material) override;
    Result<> addFeature(std::unique_ptr<render::IRenderFeature> feature) override;
    bool removeFeature(std::string_view name) override;
    void setClearColor(const Vec4& color) override { clearColor_ = color; }
    [[nodiscard]] Extent2D renderExtent() const override { return extent_; }
    void requestScreenshot(const std::filesystem::path& path) override { screenshotPath_ = path; }

    // --- Used by the built-in mesh feature ----------------------------------------
    [[nodiscard]] const GpuMesh* mesh(render::MeshHandle handle) const {
        return meshes_.get(handle);
    }
    /// Falls back to the default (white) material for invalid handles.
    [[nodiscard]] const GpuMaterial& materialOrDefault(render::MaterialHandle handle) const;
    [[nodiscard]] rhi::BindGroupLayoutHandle materialLayout() const { return materialLayout_; }

private:
    [[nodiscard]] Extent2D targetExtent() const;
    void ensureDepthTarget(Extent2D extent);
    void ensureCaptureTarget(Extent2D extent);
    void updateFrameUniforms(const render::CameraData& camera, Extent2D extent,
                             const FrameTime& time);
    void renderView(rhi::ICommandList& commands, rhi::TextureHandle target, Extent2D extent,
                    const render::CameraData& camera, const FrameTime& time);
    void saveCapture(const std::filesystem::path& path);
    [[nodiscard]] render::RenderSetupContext setupContext();

    Engine& engine_;
    rhi::IDevice& device_;
    platform::IWindow* window_ = nullptr;  ///< Null when no window system is loaded.

    // Output
    rhi::SwapchainHandle swapchain_;  ///< Invalid when rendering offscreen.
    rhi::TextureHandle depthTarget_;
    rhi::TextureHandle captureTarget_;  ///< Offscreen color target (headless, screenshots).
    Extent2D depthExtent_;
    Extent2D captureExtent_;
    Extent2D extent_;
    Extent2D fallbackExtent_{1280, 720};
    rhi::Format colorFormat_ = rhi::Format::RGBA8Srgb;
    rhi::Format depthFormat_ = rhi::Format::D32Float;
    Vec4 clearColor_{0.02f, 0.02f, 0.03f, 1.0f};
    std::filesystem::path screenshotPath_;

    // Frame constants: one buffer + bind group per frame in flight, so the CPU
    // never rewrites data the GPU is still reading.
    rhi::BindGroupLayoutHandle frameLayout_;
    std::vector<rhi::BufferHandle> frameBuffers_;
    std::vector<rhi::BindGroupHandle> frameBindGroups_;

    // Scene resources
    rhi::BindGroupLayoutHandle materialLayout_;
    rhi::SamplerHandle defaultSampler_;
    render::TextureHandle whiteTexture_;
    render::MaterialHandle defaultMaterial_;
    HandlePool<GpuMesh, render::MeshTag> meshes_;
    HandlePool<rhi::TextureHandle, render::TextureTag> textures_;
    HandlePool<GpuMaterial, render::MaterialTag> materials_;

    std::vector<std::unique_ptr<render::IRenderFeature>> features_;
    bool initialized_ = false;
};

}  // namespace bdex::forward
