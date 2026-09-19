#pragma once

#include <bdex/assets/Image.hpp>
#include <bdex/assets/Mesh.hpp>
#include <bdex/core/Math.hpp>
#include <bdex/core/ServiceRegistry.hpp>
#include <bdex/core/Time.hpp>
#include <bdex/ecs/World.hpp>
#include <bdex/render/Components.hpp>
#include <bdex/rhi/Device.hpp>

#include <filesystem>
#include <memory>
#include <string_view>

namespace bdex::render {

/// View parameters of the frame being rendered.
struct CameraData {
    Mat4 view{1.0f};
    Mat4 projection{1.0f};
    Mat4 viewProjection{1.0f};
    Vec3 position{0.0f};
};

[[nodiscard]] CameraData makeCameraData(const Mat4& cameraWorld, const Camera& camera,
                                        f32 aspectRatio);

/// Per-frame constants bound as set 0, binding 0 for every render feature
/// (std140 compatible: only vec4 / mat4 members).
struct FrameUniforms {
    Mat4 view{1.0f};
    Mat4 projection{1.0f};
    Mat4 viewProjection{1.0f};
    Mat4 inverseViewProjection{1.0f};
    Vec4 cameraPosition{0.0f};  ///< xyz
    Vec4 lightDirection{0.0f};  ///< xyz, normalized, direction of travel
    Vec4 lightColor{1.0f};      ///< rgb premultiplied by intensity
    Vec4 ambientColor{0.0f};    ///< rgb
    Vec4 timeAndExtent{0.0f};   ///< elapsed seconds, delta seconds, width, height
};

struct MaterialDesc {
    Vec4 baseColor{1.0f};
    TextureHandle albedo;  ///< Optional; multiplies baseColor.
    f32 specular = 0.3f;
    f32 shininess = 32.0f;
};

/// Everything a render feature needs to create its GPU objects.
struct RenderSetupContext {
    rhi::IDevice& device;
    ServiceRegistry& services;
    /// Formats of the targets features draw into; pipelines must match them.
    rhi::Format colorFormat;
    rhi::Format depthFormat;
    /// Layout of set 0 (FrameUniforms).
    rhi::BindGroupLayoutHandle frameLayout;
};

/// Everything a render feature needs to draw. Rendering has already begun:
/// the feature only binds state and issues draws.
struct RenderFrameContext {
    rhi::IDevice& device;
    rhi::ICommandList& commands;
    /// World of the active scene, or nullptr when there is none.
    ecs::World* world;
    CameraData camera;
    Extent2D extent;
    /// Bind group for set 0 (FrameUniforms) of the current frame.
    rhi::BindGroupHandle frameBindGroup;
    FrameTime time;
};

/// A unit of rendering plugged into a renderer: mesh drawing, a sky, debug
/// lines, UI... Features are how applications and modules extend a renderer
/// without modifying it.
class IRenderFeature {
public:
    virtual ~IRenderFeature() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    /// Features draw by ascending order. Built-in scene geometry is at 0;
    /// use negative values for backgrounds, positive for overlays.
    [[nodiscard]] virtual i32 order() const { return 0; }

    /// Create pipelines and resources.
    virtual Result<> onAttach(const RenderSetupContext& context) = 0;
    /// Destroy everything created in onAttach.
    virtual void onDetach(rhi::IDevice& device) = 0;
    virtual void render(const RenderFrameContext& context) = 0;
};

/// Service provided by a renderer module ("renderer" capability).
///
/// A renderer draws the active scene (see scene::SceneManager) every frame
/// during Phase::Render. Scene content refers to renderer resources through
/// the handles returned here.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // --- Resources -----------------------------------------------------------
    [[nodiscard]] virtual MeshHandle createMesh(const assets::MeshData& mesh) = 0;
    virtual void destroyMesh(MeshHandle mesh) = 0;

    [[nodiscard]] virtual TextureHandle createTexture(const assets::ImageData& image) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;

    [[nodiscard]] virtual MaterialHandle createMaterial(const MaterialDesc& desc) = 0;
    virtual void destroyMaterial(MaterialHandle material) = 0;

    // --- Extension -------------------------------------------------------------
    virtual Result<> addFeature(std::unique_ptr<IRenderFeature> feature) = 0;
    /// Returns false if no feature has that name.
    virtual bool removeFeature(std::string_view name) = 0;

    // --- Output ----------------------------------------------------------------
    virtual void setClearColor(const Vec4& color) = 0;
    [[nodiscard]] virtual Extent2D renderExtent() const = 0;
    /// Saves the next rendered frame as a PNG file.
    virtual void requestScreenshot(const std::filesystem::path& path) = 0;
};

/// Published once a requested screenshot has been written (or has failed).
struct ScreenshotSaved {
    std::filesystem::path path;
    bool success = false;
};

}  // namespace bdex::render
