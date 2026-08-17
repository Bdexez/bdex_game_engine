#pragma once

#include <bdex/core/Result.hpp>
#include <bdex/rhi/Types.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace bdex::rhi {

/// Records GPU commands for the current frame. Obtained from
/// IDevice::beginFrame() and valid until IDevice::endFrame().
class ICommandList {
public:
    virtual ~ICommandList() = default;

    /// Starts rendering into the given attachments. The backend moves the
    /// textures to the right state on its own; when rendering ends, color
    /// attachments created with TextureUsage::Sampled become readable by
    /// shaders again.
    virtual void beginRendering(const RenderingDesc& desc) = 0;
    virtual void endRendering() = 0;

    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const Rect& scissor) = 0;

    virtual void bindPipeline(PipelineHandle pipeline) = 0;
    /// Binds `group` as set `setIndex` of the currently bound pipeline.
    virtual void bindBindGroup(u32 setIndex, BindGroupHandle group) = 0;
    virtual void bindVertexBuffer(BufferHandle buffer, u64 offset = 0) = 0;
    virtual void bindIndexBuffer(BufferHandle buffer, IndexType type, u64 offset = 0) = 0;
    /// Uploads push constants for the currently bound pipeline, to the stages
    /// declared in GraphicsPipelineDesc::pushConstantStages.
    virtual void pushConstants(u32 offset, std::span<const std::byte> data) = 0;

    virtual void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0,
                      u32 firstInstance = 0) = 0;
    virtual void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0,
                             i32 vertexOffset = 0, u32 firstInstance = 0) = 0;

    /// Named regions shown by GPU debuggers (RenderDoc...).
    virtual void beginDebugGroup(std::string_view name) = 0;
    virtual void endDebugGroup() = 0;
};

/// Service provided by an RHI module ("rhi" capability): creates GPU
/// resources and drives frames.
///
/// Frame protocol:
///
///   ICommandList& cmd = device.beginFrame();
///   TextureHandle target = device.acquireSwapchainTexture(swapchain);
///   if (target) { cmd.beginRendering(...); ...; cmd.endRendering(); }
///   device.endFrame();   // submits, and presents every acquired swapchain
///
/// Destroying a resource is always safe: the backend defers the actual
/// release until the GPU no longer uses it. All calls are main-thread only.
class IDevice {
public:
    virtual ~IDevice() = default;

    [[nodiscard]] virtual DeviceInfo info() const = 0;

    // --- Resources -----------------------------------------------------------
    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual void destroyBuffer(BufferHandle buffer) = 0;
    /// Copies `data` into the buffer. Immediate for CpuToGpu buffers; for
    /// GpuOnly buffers this performs a blocking staged upload (load time).
    virtual void writeBuffer(BufferHandle buffer, std::span<const std::byte> data,
                             u64 offset = 0) = 0;

    [[nodiscard]] virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    /// Uploads tightly packed pixels (blocking). Requires TextureUsage::Sampled
    /// or TransferDst.
    virtual void writeTexture(TextureHandle texture, std::span<const std::byte> pixels) = 0;
    /// Reads a color texture back as tightly packed RGBA8 (blocking; captures
    /// and tests). Requires TextureUsage::TransferSrc. Must not be called
    /// between beginFrame() and endFrame().
    [[nodiscard]] virtual std::vector<u8> readTexture(TextureHandle texture) = 0;
    /// Description of a texture, including swapchain textures. Width and
    /// height are 0 for an invalid handle.
    [[nodiscard]] virtual TextureDesc textureDesc(TextureHandle texture) const = 0;

    [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;
    virtual void destroySampler(SamplerHandle sampler) = 0;

    [[nodiscard]] virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual void destroyShader(ShaderHandle shader) = 0;

    [[nodiscard]] virtual BindGroupLayoutHandle createBindGroupLayout(
        const BindGroupLayoutDesc& desc) = 0;
    virtual void destroyBindGroupLayout(BindGroupLayoutHandle layout) = 0;

    [[nodiscard]] virtual BindGroupHandle createBindGroup(const BindGroupDesc& desc) = 0;
    virtual void destroyBindGroup(BindGroupHandle group) = 0;

    [[nodiscard]] virtual PipelineHandle createGraphicsPipeline(
        const GraphicsPipelineDesc& desc) = 0;
    virtual void destroyPipeline(PipelineHandle pipeline) = 0;

    // --- Presentation ----------------------------------------------------------
    /// Creates a swapchain presenting to `desc.window`. The backend tracks the
    /// window size and recreates the swapchain images when it changes.
    [[nodiscard]] virtual Result<SwapchainHandle> createSwapchain(const SwapchainDesc& desc) = 0;
    virtual void destroySwapchain(SwapchainHandle swapchain) = 0;
    [[nodiscard]] virtual Format swapchainFormat(SwapchainHandle swapchain) const = 0;

    // --- Frame -------------------------------------------------------------------
    /// Number of frames the CPU may record ahead of the GPU. Data rewritten
    /// every frame (uniform buffers) needs one copy per frame slot.
    [[nodiscard]] virtual u32 framesInFlight() const = 0;
    /// Slot of the frame being recorded, in [0, framesInFlight()).
    [[nodiscard]] virtual u32 frameSlot() const = 0;

    virtual ICommandList& beginFrame() = 0;
    /// Returns the texture to render to for this frame, or an invalid handle
    /// when nothing can be presented right now (minimized window, swapchain
    /// being resized): skip rendering to it, the frame must still be ended.
    [[nodiscard]] virtual TextureHandle acquireSwapchainTexture(SwapchainHandle swapchain) = 0;
    virtual void endFrame() = 0;

    /// Blocks until the GPU has finished all submitted work.
    virtual void waitIdle() = 0;

    // --- Diagnostics -------------------------------------------------------------
    /// Statistics of the last completed frame.
    [[nodiscard]] virtual FrameStats lastFrameStats() const = 0;
    /// Number of errors reported by the backend's validation facilities since
    /// creation. Always 0 when validation is off.
    [[nodiscard]] virtual u32 validationErrorCount() const = 0;
};

/// Convenience for the `std::span<const std::byte>` parameters.
template <class T>
[[nodiscard]] std::span<const std::byte> asBytes(const T& value) {
    return std::as_bytes(std::span<const T, 1>(&value, 1));
}

template <class T>
[[nodiscard]] std::span<const std::byte> asBytes(std::span<const T> values) {
    return std::as_bytes(values);
}

template <class T>
[[nodiscard]] std::span<const std::byte> asBytes(const std::vector<T>& values) {
    return std::as_bytes(std::span<const T>(values));
}

}  // namespace bdex::rhi
