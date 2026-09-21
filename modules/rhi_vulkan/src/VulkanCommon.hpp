#pragma once

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <bdex/core/Assert.hpp>
#include <bdex/core/Log.hpp>
#include <bdex/core/Types.hpp>
#include <bdex/rhi/Types.hpp>

namespace bdex::vulkan {

/// Frames the CPU may record ahead of the GPU.
inline constexpr u32 kFramesInFlight = 2;

[[nodiscard]] const char* resultString(VkResult result);

// --- RHI -> Vulkan conversions -------------------------------------------------

[[nodiscard]] VkFormat toVkFormat(rhi::Format format);
/// Returns Format::Undefined for formats the RHI does not expose.
[[nodiscard]] rhi::Format fromVkFormat(VkFormat format);
[[nodiscard]] VkImageAspectFlags aspectMask(rhi::Format format);
[[nodiscard]] VkBufferUsageFlags toVkBufferUsage(rhi::BufferUsage usage);
[[nodiscard]] VkImageUsageFlags toVkImageUsage(rhi::TextureUsage usage);
[[nodiscard]] VkShaderStageFlags toVkShaderStages(rhi::ShaderStage stages);
[[nodiscard]] VkFilter toVkFilter(rhi::Filter filter);
[[nodiscard]] VkSamplerAddressMode toVkAddressMode(rhi::AddressMode mode);
[[nodiscard]] VkFormat toVkVertexFormat(rhi::VertexFormat format);
[[nodiscard]] VkPrimitiveTopology toVkTopology(rhi::PrimitiveTopology topology);
[[nodiscard]] VkCullModeFlags toVkCullMode(rhi::CullMode mode);
[[nodiscard]] VkFrontFace toVkFrontFace(rhi::FrontFace face);
[[nodiscard]] VkCompareOp toVkCompareOp(rhi::CompareOp op);
[[nodiscard]] VkAttachmentLoadOp toVkLoadOp(rhi::LoadOp op);
[[nodiscard]] VkAttachmentStoreOp toVkStoreOp(rhi::StoreOp op);
[[nodiscard]] VkDescriptorType toVkDescriptorType(rhi::BindingType type);

}  // namespace bdex::vulkan

/// For Vulkan calls that can only fail on unrecoverable conditions (device
/// lost, out of memory) or API misuse: logs the failing call and aborts.
#define BDEX_VK_CHECK(call)                                                          \
    do {                                                                             \
        const VkResult bdexVkResult = (call);                                        \
        if (bdexVkResult != VK_SUCCESS) [[unlikely]] {                               \
            ::bdex::log::error("vulkan", "{} failed: {}", #call,                     \
                               ::bdex::vulkan::resultString(bdexVkResult));          \
            BDEX_ASSERT(false, "unrecoverable Vulkan error");                        \
        }                                                                            \
    } while (false)
