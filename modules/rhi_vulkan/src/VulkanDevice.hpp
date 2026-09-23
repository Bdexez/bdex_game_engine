#pragma once

#include "VulkanCommandList.hpp"
#include "VulkanCommon.hpp"

#include <bdex/core/Handle.hpp>
#include <bdex/core/Result.hpp>
#include <bdex/platform/Window.hpp>
#include <bdex/rhi/Device.hpp>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bdex::vulkan {

struct DeviceConfig {
    std::string applicationName = "bdex";
    /// Enable the Khronos validation layer and debug names when available.
    bool validation = false;
    /// Instance extensions required by the window system (surface creation).
    std::vector<std::string> instanceExtensions;
    /// Enable swapchain support. False when running headless.
    bool presentation = false;
    /// Optional case-sensitive substring of the adapter name to prefer.
    std::string preferredAdapter;
};

// --- Backend objects behind the RHI handles ------------------------------------

struct Buffer {
    VkBuffer handle = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;  ///< Non-null for host-visible buffers.
    rhi::BufferDesc desc;
};

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;  ///< Null for swapchain images.
    VkFormat format = VK_FORMAT_UNDEFINED;
    /// Layout the image is in at this point of the command stream. Tracking it
    /// here is what lets the RHI hide layout transitions from its users.
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    rhi::TextureDesc desc;
    bool swapchainImage = false;
};

struct Sampler {
    VkSampler handle = VK_NULL_HANDLE;
};

struct Shader {
    VkShaderModule module = VK_NULL_HANDLE;
    rhi::ShaderStage stage = rhi::ShaderStage::None;
    std::string entryPoint;
};

struct BindGroupLayout {
    VkDescriptorSetLayout handle = VK_NULL_HANDLE;
    rhi::BindGroupLayoutDesc desc;
};

struct BindGroup {
    VkDescriptorSet handle = VK_NULL_HANDLE;
};

struct Pipeline {
    VkPipeline handle = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderStageFlags pushConstantStages = 0;
};

struct Swapchain {
    platform::IWindow* window = nullptr;
    bool vsync = true;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    rhi::Format format = rhi::Format::Undefined;
    VkExtent2D extent{};
    /// Window framebuffer size the swapchain was built for; a mismatch means
    /// the window was resized.
    Extent2D windowSize;
    std::vector<rhi::TextureHandle> images;
    /// Signaled when rendering to image i is done; waited on by present.
    std::vector<VkSemaphore> renderFinished;
    /// Signaled when the acquired image is ready; one per frame slot.
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    u32 imageIndex = 0;
    bool acquired = false;
    bool outOfDate = false;
};

/// Vulkan 1.3 implementation of rhi::IDevice (dynamic rendering,
/// synchronization2, VMA for memory).
class Device final : public rhi::IDevice {
public:
    [[nodiscard]] static Result<std::unique_ptr<Device>> create(const DeviceConfig& config);
    ~Device() override;

    [[nodiscard]] rhi::DeviceInfo info() const override;

    [[nodiscard]] rhi::BufferHandle createBuffer(const rhi::BufferDesc& desc) override;
    void destroyBuffer(rhi::BufferHandle buffer) override;
    void writeBuffer(rhi::BufferHandle buffer, std::span<const std::byte> data, u64 offset) override;

    [[nodiscard]] rhi::TextureHandle createTexture(const rhi::TextureDesc& desc) override;
    void destroyTexture(rhi::TextureHandle texture) override;
    void writeTexture(rhi::TextureHandle texture, std::span<const std::byte> pixels) override;
    [[nodiscard]] std::vector<u8> readTexture(rhi::TextureHandle texture) override;
    [[nodiscard]] rhi::TextureDesc textureDesc(rhi::TextureHandle texture) const override;

    [[nodiscard]] rhi::SamplerHandle createSampler(const rhi::SamplerDesc& desc) override;
    void destroySampler(rhi::SamplerHandle sampler) override;

    [[nodiscard]] rhi::ShaderHandle createShader(const rhi::ShaderDesc& desc) override;
    void destroyShader(rhi::ShaderHandle shader) override;

    [[nodiscard]] rhi::BindGroupLayoutHandle createBindGroupLayout(
        const rhi::BindGroupLayoutDesc& desc) override;
    void destroyBindGroupLayout(rhi::BindGroupLayoutHandle layout) override;

    [[nodiscard]] rhi::BindGroupHandle createBindGroup(const rhi::BindGroupDesc& desc) override;
    void destroyBindGroup(rhi::BindGroupHandle group) override;

    [[nodiscard]] rhi::PipelineHandle createGraphicsPipeline(
        const rhi::GraphicsPipelineDesc& desc) override;
    void destroyPipeline(rhi::PipelineHandle pipeline) override;

    [[nodiscard]] Result<rhi::SwapchainHandle> createSwapchain(
        const rhi::SwapchainDesc& desc) override;
    void destroySwapchain(rhi::SwapchainHandle swapchain) override;
    [[nodiscard]] rhi::Format swapchainFormat(rhi::SwapchainHandle swapchain) const override;

    [[nodiscard]] u32 framesInFlight() const override { return kFramesInFlight; }
    [[nodiscard]] u32 frameSlot() const override {
        return static_cast<u32>(frameNumber_ % kFramesInFlight);
    }

    rhi::ICommandList& beginFrame() override;
    [[nodiscard]] rhi::TextureHandle acquireSwapchainTexture(
        rhi::SwapchainHandle swapchain) override;
    void endFrame() override;
    void waitIdle() override;

    [[nodiscard]] rhi::FrameStats lastFrameStats() const override { return lastStats_; }
    [[nodiscard]] u32 validationErrorCount() const override {
        return validationErrors_.load(std::memory_order_relaxed);
    }

    // --- Internal API used by CommandList ----------------------------------------
    [[nodiscard]] Buffer* buffer(rhi::BufferHandle handle) { return buffers_.get(handle); }
    [[nodiscard]] Texture* texture(rhi::TextureHandle handle) { return textures_.get(handle); }
    [[nodiscard]] BindGroup* bindGroup(rhi::BindGroupHandle handle) { return bindGroups_.get(handle); }
    [[nodiscard]] Pipeline* pipeline(rhi::PipelineHandle handle) { return pipelines_.get(handle); }

    /// Records a layout transition if the texture is not already in `layout`.
    void transition(VkCommandBuffer commandBuffer, Texture& texture, VkImageLayout layout);

    void beginDebugLabel(VkCommandBuffer commandBuffer, std::string_view name);
    void endDebugLabel(VkCommandBuffer commandBuffer);

private:
    Device() : commandList_(*this) {}

    struct FrameData {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    struct DeferredRelease {
        u64 frame = 0;  ///< Last frame that may reference the object.
        std::function<void()> release;
    };

    Result<> initialize(const DeviceConfig& config);
    Result<> createInstance(const DeviceConfig& config);
    Result<> selectPhysicalDevice(const DeviceConfig& config);
    Result<> createLogicalDevice(const DeviceConfig& config);
    void createFrameResources();

    /// Queues a destruction until the GPU can no longer be using the object.
    void defer(std::function<void()> release);
    /// Runs the releases of every frame up to `completedFrame` included.
    void flushDeferred(u64 completedFrame);
    void flushAllDeferred();

    /// Records and submits a one-off command buffer, then waits for it.
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

    void setDebugName(VkObjectType type, u64 handle, const std::string& name);

    // Swapchain internals (VulkanSwapchain.cpp).
    Result<> buildSwapchain(Swapchain& swapchain);
    void releaseSwapchainImages(Swapchain& swapchain);
    void destroySwapchainObjects(Swapchain& swapchain);

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData);

    // --- Core objects --------------------------------------------------------
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    u32 queueFamily_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    std::string adapterName_;
    u32 adapterApiVersion_ = 0;
    bool validationEnabled_ = false;
    bool debugUtilsEnabled_ = false;
    bool presentationEnabled_ = false;
    std::atomic<u32> validationErrors_{0};

    // VK_EXT_debug_utils entry points (not exported by the loader).
    PFN_vkCreateDebugUtilsMessengerEXT createMessenger_ = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger_ = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT setObjectName_ = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT cmdBeginLabel_ = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT cmdEndLabel_ = nullptr;

    // --- Resources -------------------------------------------------------------
    HandlePool<Buffer, rhi::BufferTag> buffers_;
    HandlePool<Texture, rhi::TextureTag> textures_;
    HandlePool<Sampler, rhi::SamplerTag> samplers_;
    HandlePool<Shader, rhi::ShaderTag> shaders_;
    HandlePool<BindGroupLayout, rhi::BindGroupLayoutTag> bindGroupLayouts_;
    HandlePool<BindGroup, rhi::BindGroupTag> bindGroups_;
    HandlePool<Pipeline, rhi::PipelineTag> pipelines_;
    HandlePool<Swapchain, rhi::SwapchainTag> swapchains_;

    // --- Frame state -------------------------------------------------------------
    std::array<FrameData, kFramesInFlight> frames_{};
    CommandList commandList_;
    std::deque<DeferredRelease> deferred_;
    std::vector<rhi::SwapchainHandle> acquired_;
    rhi::FrameStats lastStats_;
    u64 frameNumber_ = 0;
    bool inFrame_ = false;

    // One-off submissions (uploads, readbacks).
    VkCommandPool immediatePool_ = VK_NULL_HANDLE;
    VkCommandBuffer immediateCommandBuffer_ = VK_NULL_HANDLE;
    VkFence immediateFence_ = VK_NULL_HANDLE;
};

}  // namespace bdex::vulkan
