#pragma once

#include "VulkanCommon.hpp"

#include <bdex/rhi/Device.hpp>

#include <vector>

namespace bdex::vulkan {

class Device;
struct Pipeline;

/// ICommandList over the command buffer of the frame being recorded.
class CommandList final : public rhi::ICommandList {
public:
    explicit CommandList(Device& device) : device_(&device) {}

    /// Called by the device at the start / end of each frame.
    void begin(VkCommandBuffer commandBuffer);
    void end();

    [[nodiscard]] VkCommandBuffer handle() const { return commandBuffer_; }
    [[nodiscard]] const rhi::FrameStats& stats() const { return stats_; }
    [[nodiscard]] bool rendering() const { return rendering_; }

    void beginRendering(const rhi::RenderingDesc& desc) override;
    void endRendering() override;
    void setViewport(const rhi::Viewport& viewport) override;
    void setScissor(const rhi::Rect& scissor) override;
    void bindPipeline(rhi::PipelineHandle pipeline) override;
    void bindBindGroup(u32 setIndex, rhi::BindGroupHandle group) override;
    void bindVertexBuffer(rhi::BufferHandle buffer, u64 offset) override;
    void bindIndexBuffer(rhi::BufferHandle buffer, rhi::IndexType type, u64 offset) override;
    void pushConstants(u32 offset, std::span<const std::byte> data) override;
    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                     u32 firstInstance) override;
    void beginDebugGroup(std::string_view name) override;
    void endDebugGroup() override;

private:
    Device* device_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    const Pipeline* boundPipeline_ = nullptr;
    std::vector<rhi::TextureHandle> colorTargets_;
    rhi::FrameStats stats_;
    bool rendering_ = false;
};

}  // namespace bdex::vulkan
