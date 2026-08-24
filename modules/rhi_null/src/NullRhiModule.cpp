// rhi_null: an RHI backend that talks to no GPU.
//
// Every resource is tracked (so handle lifetimes and frame protocol misuse
// are still caught) and every command is counted but nothing is drawn. It
// makes the full engine - renderer included - runnable in unit tests and on
// machines without a graphics driver, and it doubles as the smallest possible
// example of a backend.

#include <bdex/core/Assert.hpp>
#include <bdex/core/Engine.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/core/Module.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/rhi/Device.hpp>

#include <memory>

namespace bdex {
namespace {

using namespace rhi;

class NullCommandList final : public ICommandList {
public:
    void beginRendering(const RenderingDesc&) override {
        BDEX_ASSERT(!rendering_, "beginRendering called twice");
        rendering_ = true;
    }
    void endRendering() override {
        BDEX_ASSERT(rendering_, "endRendering without beginRendering");
        rendering_ = false;
    }

    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}
    void bindPipeline(PipelineHandle) override {}
    void bindBindGroup(u32, BindGroupHandle) override {}
    void bindVertexBuffer(BufferHandle, u64) override {}
    void bindIndexBuffer(BufferHandle, IndexType, u64) override {}
    void pushConstants(u32, std::span<const std::byte>) override {}

    void draw(u32 vertexCount, u32 instanceCount, u32, u32) override {
        BDEX_ASSERT(rendering_, "draw outside of a rendering scope");
        ++stats.drawCalls;
        stats.triangles += static_cast<u64>(vertexCount / 3) * instanceCount;
    }
    void drawIndexed(u32 indexCount, u32 instanceCount, u32, i32, u32) override {
        BDEX_ASSERT(rendering_, "draw outside of a rendering scope");
        ++stats.drawCalls;
        stats.triangles += static_cast<u64>(indexCount / 3) * instanceCount;
    }

    void beginDebugGroup(std::string_view) override {}
    void endDebugGroup() override {}

    [[nodiscard]] bool rendering() const { return rendering_; }

    FrameStats stats;

private:
    bool rendering_ = false;
};

class NullDevice final : public IDevice {
public:
    [[nodiscard]] DeviceInfo info() const override {
        return DeviceInfo{"null", "Null device", "n/a", false};
    }

    // --- Resources -----------------------------------------------------------
    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc) override {
        return buffers_.insert(desc);
    }
    void destroyBuffer(BufferHandle buffer) override { buffers_.remove(buffer); }
    void writeBuffer(BufferHandle buffer, std::span<const std::byte> data, u64 offset) override {
        const BufferDesc* desc = buffers_.get(buffer);
        BDEX_ASSERT(desc != nullptr, "writeBuffer on an invalid buffer");
        BDEX_ASSERT(offset + data.size() <= desc->size, "writeBuffer out of bounds");
    }

    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc) override {
        return textures_.insert(desc);
    }
    void destroyTexture(TextureHandle texture) override { textures_.remove(texture); }
    void writeTexture(TextureHandle texture, std::span<const std::byte> pixels) override {
        const TextureDesc* desc = textures_.get(texture);
        BDEX_ASSERT(desc != nullptr, "writeTexture on an invalid texture");
        BDEX_ASSERT(pixels.size() == static_cast<usize>(desc->width) * desc->height *
                                         bytesPerPixel(desc->format),
                    "writeTexture size mismatch");
    }
    [[nodiscard]] std::vector<u8> readTexture(TextureHandle texture) override {
        const TextureDesc* desc = textures_.get(texture);
        BDEX_ASSERT(desc != nullptr, "readTexture on an invalid texture");
        // Opaque black: there is no image to read back.
        std::vector<u8> pixels(static_cast<usize>(desc->width) * desc->height * 4, 0);
        for (usize i = 3; i < pixels.size(); i += 4) {
            pixels[i] = 255;
        }
        return pixels;
    }
    [[nodiscard]] TextureDesc textureDesc(TextureHandle texture) const override {
        if (const TextureDesc* desc = textures_.get(texture)) {
            return *desc;
        }
        TextureDesc invalid;
        invalid.width = 0;
        invalid.height = 0;
        invalid.format = Format::Undefined;
        return invalid;
    }

    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc) override {
        return samplers_.insert(desc);
    }
    void destroySampler(SamplerHandle sampler) override { samplers_.remove(sampler); }

    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& desc) override {
        return shaders_.insert(desc.stage);
    }
    void destroyShader(ShaderHandle shader) override { shaders_.remove(shader); }

    [[nodiscard]] BindGroupLayoutHandle createBindGroupLayout(
        const BindGroupLayoutDesc& desc) override {
        return layouts_.insert(desc);
    }
    void destroyBindGroupLayout(BindGroupLayoutHandle layout) override { layouts_.remove(layout); }

    [[nodiscard]] BindGroupHandle createBindGroup(const BindGroupDesc& desc) override {
        BDEX_ASSERT(layouts_.contains(desc.layout), "bind group created with an invalid layout");
        return groups_.insert(desc);
    }
    void destroyBindGroup(BindGroupHandle group) override { groups_.remove(group); }

    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        BDEX_ASSERT(shaders_.contains(desc.vertexShader) && shaders_.contains(desc.fragmentShader),
                    "pipeline created with invalid shaders");
        return pipelines_.insert(desc);
    }
    void destroyPipeline(PipelineHandle pipeline) override { pipelines_.remove(pipeline); }

    // --- Presentation ----------------------------------------------------------
    [[nodiscard]] Result<SwapchainHandle> createSwapchain(const SwapchainDesc& desc) override {
        if (desc.window == nullptr) {
            return fail("a swapchain needs a window");
        }
        Swapchain swapchain;
        swapchain.window = desc.window;
        return swapchains_.insert(swapchain);
    }
    void destroySwapchain(SwapchainHandle handle) override {
        if (const auto swapchain = swapchains_.take(handle)) {
            textures_.remove(swapchain->backbuffer);
        }
    }
    [[nodiscard]] Format swapchainFormat(SwapchainHandle) const override {
        return Format::BGRA8Srgb;
    }

    // --- Frame -------------------------------------------------------------------
    [[nodiscard]] u32 framesInFlight() const override { return 2; }
    [[nodiscard]] u32 frameSlot() const override { return static_cast<u32>(frameNumber_ % 2); }

    ICommandList& beginFrame() override {
        BDEX_ASSERT(!inFrame_, "beginFrame called twice");
        inFrame_ = true;
        commands_.stats = {};
        return commands_;
    }

    [[nodiscard]] TextureHandle acquireSwapchainTexture(SwapchainHandle handle) override {
        BDEX_ASSERT(inFrame_, "acquireSwapchainTexture outside of a frame");
        Swapchain* swapchain = swapchains_.get(handle);
        if (swapchain == nullptr) {
            return {};
        }
        const Extent2D size = swapchain->window->framebufferSize();
        if (size.width == 0 || size.height == 0) {
            return {};
        }
        // Mimic a real swapchain: the backbuffer follows the window size.
        if (size != swapchain->extent) {
            textures_.remove(swapchain->backbuffer);
            TextureDesc desc;
            desc.width = size.width;
            desc.height = size.height;
            desc.format = Format::BGRA8Srgb;
            desc.usage = TextureUsage::ColorAttachment;
            desc.debugName = "null backbuffer";
            swapchain->backbuffer = textures_.insert(desc);
            swapchain->extent = size;
        }
        return swapchain->backbuffer;
    }

    void endFrame() override {
        BDEX_ASSERT(inFrame_, "endFrame without beginFrame");
        BDEX_ASSERT(!commands_.rendering(), "endFrame inside a rendering scope");
        inFrame_ = false;
        lastStats_ = commands_.stats;
        ++frameNumber_;
    }

    void waitIdle() override {}

    [[nodiscard]] FrameStats lastFrameStats() const override { return lastStats_; }
    [[nodiscard]] u32 validationErrorCount() const override { return 0; }

    /// Number of resources still alive (leak detection at shutdown).
    [[nodiscard]] usize liveResourceCount() const {
        return buffers_.size() + textures_.size() + samplers_.size() + shaders_.size() +
               layouts_.size() + groups_.size() + pipelines_.size() + swapchains_.size();
    }

private:
    struct Swapchain {
        platform::IWindow* window = nullptr;
        TextureHandle backbuffer;
        Extent2D extent;
    };

    HandlePool<BufferDesc, BufferTag> buffers_;
    HandlePool<TextureDesc, TextureTag> textures_;
    HandlePool<SamplerDesc, SamplerTag> samplers_;
    HandlePool<ShaderStage, ShaderTag> shaders_;
    HandlePool<BindGroupLayoutDesc, BindGroupLayoutTag> layouts_;
    HandlePool<BindGroupDesc, BindGroupTag> groups_;
    HandlePool<GraphicsPipelineDesc, PipelineTag> pipelines_;
    HandlePool<Swapchain, SwapchainTag> swapchains_;

    NullCommandList commands_;
    FrameStats lastStats_;
    u64 frameNumber_ = 0;
    bool inFrame_ = false;
};

class NullRhiModule final : public IModule {
public:
    [[nodiscard]] ModuleInfo info() const override {
        return ModuleInfo{
            .name = BDEX_MODULE_NAME_STRING,
            .description = "No-op render hardware interface",
            .provides = {"rhi"},
            .dependencies = {},
            .after = {"window"},
        };
    }

    Result<> onInit(Engine& engine) override {
        device_ = std::make_shared<NullDevice>();
        engine.services().provide<IDevice>(device_);
        return {};
    }

    void onShutdown(Engine& engine) override {
        engine.services().remove<IDevice>();
        if (device_ != nullptr && device_->liveResourceCount() != 0) {
            log::warn("rhi_null", "{} resource(s) were never destroyed",
                      device_->liveResourceCount());
        }
        device_.reset();
    }

private:
    std::shared_ptr<NullDevice> device_;
};

}  // namespace
}  // namespace bdex

BDEX_DEFINE_MODULE(bdex::NullRhiModule)
