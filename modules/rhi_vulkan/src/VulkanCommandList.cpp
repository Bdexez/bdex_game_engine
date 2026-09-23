#include "VulkanCommandList.hpp"

#include "VulkanDevice.hpp"

#include <array>

namespace bdex::vulkan {

void CommandList::begin(VkCommandBuffer commandBuffer) {
    commandBuffer_ = commandBuffer;
    boundPipeline_ = nullptr;
    colorTargets_.clear();
    stats_ = {};
    rendering_ = false;
}

void CommandList::end() {
    BDEX_ASSERT(!rendering_, "endFrame called inside a rendering scope");
    commandBuffer_ = VK_NULL_HANDLE;
    boundPipeline_ = nullptr;
}

void CommandList::beginRendering(const rhi::RenderingDesc& desc) {
    BDEX_ASSERT(!rendering_, "beginRendering called twice");
    BDEX_ASSERT(!desc.colorAttachments.empty() || desc.depthAttachment != nullptr,
                "beginRendering needs at least one attachment");

    constexpr usize kMaxColorAttachments = 8;
    BDEX_ASSERT(desc.colorAttachments.size() <= kMaxColorAttachments);
    std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> colors{};
    VkExtent2D extent{};
    colorTargets_.clear();

    for (usize i = 0; i < desc.colorAttachments.size(); ++i) {
        const rhi::ColorAttachment& attachment = desc.colorAttachments[i];
        Texture* texture = device_->texture(attachment.texture);
        BDEX_ASSERT(texture != nullptr, "invalid color attachment");
        device_->transition(commandBuffer_, *texture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        VkRenderingAttachmentInfo& info = colors[i];
        info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        info.imageView = texture->view;
        info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        info.loadOp = toVkLoadOp(attachment.loadOp);
        info.storeOp = toVkStoreOp(attachment.storeOp);
        info.clearValue.color = {{attachment.clearColor[0], attachment.clearColor[1],
                                  attachment.clearColor[2], attachment.clearColor[3]}};

        extent = {texture->desc.width, texture->desc.height};
        colorTargets_.push_back(attachment.texture);
    }

    VkRenderingAttachmentInfo depth{};
    bool depthHasStencil = false;
    if (desc.depthAttachment != nullptr) {
        Texture* texture = device_->texture(desc.depthAttachment->texture);
        BDEX_ASSERT(texture != nullptr, "invalid depth attachment");
        device_->transition(commandBuffer_, *texture,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth.imageView = texture->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = toVkLoadOp(desc.depthAttachment->loadOp);
        depth.storeOp = toVkStoreOp(desc.depthAttachment->storeOp);
        depth.clearValue.depthStencil = {desc.depthAttachment->clearDepth, 0};

        depthHasStencil = rhi::hasStencil(texture->desc.format);
        extent = {texture->desc.width, texture->desc.height};
    }

    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, extent};
    info.layerCount = 1;
    info.colorAttachmentCount = static_cast<u32>(desc.colorAttachments.size());
    info.pColorAttachments = colors.data();
    info.pDepthAttachment = desc.depthAttachment != nullptr ? &depth : nullptr;
    info.pStencilAttachment = depthHasStencil ? &depth : nullptr;
    vkCmdBeginRendering(commandBuffer_, &info);
    rendering_ = true;
}

void CommandList::endRendering() {
    BDEX_ASSERT(rendering_, "endRendering without beginRendering");
    vkCmdEndRendering(commandBuffer_);
    rendering_ = false;

    // Render targets that can be sampled go back to a shader-readable state so
    // later passes can bind them without any explicit state management.
    for (const rhi::TextureHandle handle : colorTargets_) {
        Texture* texture = device_->texture(handle);
        if (texture != nullptr && !texture->swapchainImage &&
            hasFlag(texture->desc.usage, rhi::TextureUsage::Sampled)) {
            device_->transition(commandBuffer_, *texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    colorTargets_.clear();
}

void CommandList::setViewport(const rhi::Viewport& viewport) {
    const VkViewport vkViewport{viewport.x,     viewport.y,        viewport.width,
                                viewport.height, viewport.minDepth, viewport.maxDepth};
    vkCmdSetViewport(commandBuffer_, 0, 1, &vkViewport);
}

void CommandList::setScissor(const rhi::Rect& scissor) {
    const VkRect2D rect{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
    vkCmdSetScissor(commandBuffer_, 0, 1, &rect);
}

void CommandList::bindPipeline(rhi::PipelineHandle handle) {
    const Pipeline* pipeline = device_->pipeline(handle);
    BDEX_ASSERT(pipeline != nullptr, "invalid pipeline");
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
    boundPipeline_ = pipeline;
}

void CommandList::bindBindGroup(u32 setIndex, rhi::BindGroupHandle handle) {
    BDEX_ASSERT(boundPipeline_ != nullptr, "bind a pipeline before its bind groups");
    const BindGroup* group = device_->bindGroup(handle);
    BDEX_ASSERT(group != nullptr, "invalid bind group");
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            boundPipeline_->layout, setIndex, 1, &group->handle, 0, nullptr);
}

void CommandList::bindVertexBuffer(rhi::BufferHandle handle, u64 offset) {
    const Buffer* buffer = device_->buffer(handle);
    BDEX_ASSERT(buffer != nullptr, "invalid vertex buffer");
    const VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &buffer->handle, &vkOffset);
}

void CommandList::bindIndexBuffer(rhi::BufferHandle handle, rhi::IndexType type, u64 offset) {
    const Buffer* buffer = device_->buffer(handle);
    BDEX_ASSERT(buffer != nullptr, "invalid index buffer");
    vkCmdBindIndexBuffer(commandBuffer_, buffer->handle, offset,
                         type == rhi::IndexType::UInt16 ? VK_INDEX_TYPE_UINT16
                                                        : VK_INDEX_TYPE_UINT32);
}

void CommandList::pushConstants(u32 offset, std::span<const std::byte> data) {
    BDEX_ASSERT(boundPipeline_ != nullptr, "bind a pipeline before pushing constants");
    BDEX_ASSERT(boundPipeline_->pushConstantStages != 0,
                "the bound pipeline declares no push constants");
    vkCmdPushConstants(commandBuffer_, boundPipeline_->layout, boundPipeline_->pushConstantStages,
                       offset, static_cast<u32>(data.size()), data.data());
}

void CommandList::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
    vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
    ++stats_.drawCalls;
    stats_.triangles += static_cast<u64>(vertexCount / 3) * instanceCount;
}

void CommandList::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset,
                              u32 firstInstance) {
    vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset,
                     firstInstance);
    ++stats_.drawCalls;
    stats_.triangles += static_cast<u64>(indexCount / 3) * instanceCount;
}

void CommandList::beginDebugGroup(std::string_view name) {
    device_->beginDebugLabel(commandBuffer_, name);
}

void CommandList::endDebugGroup() {
    device_->endDebugLabel(commandBuffer_);
}

}  // namespace bdex::vulkan
