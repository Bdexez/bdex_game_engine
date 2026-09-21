#include "VulkanCommon.hpp"

namespace bdex::vulkan {

const char* resultString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        default: return "VK_ERROR_<unknown>";
    }
}

VkFormat toVkFormat(rhi::Format format) {
    switch (format) {
        case rhi::Format::Undefined: return VK_FORMAT_UNDEFINED;
        case rhi::Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case rhi::Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case rhi::Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case rhi::Format::BGRA8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case rhi::Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case rhi::Format::D32Float: return VK_FORMAT_D32_SFLOAT;
        case rhi::Format::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

rhi::Format fromVkFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM: return rhi::Format::RGBA8Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB: return rhi::Format::RGBA8Srgb;
        case VK_FORMAT_B8G8R8A8_UNORM: return rhi::Format::BGRA8Unorm;
        case VK_FORMAT_B8G8R8A8_SRGB: return rhi::Format::BGRA8Srgb;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return rhi::Format::RGBA16Float;
        case VK_FORMAT_D32_SFLOAT: return rhi::Format::D32Float;
        case VK_FORMAT_D24_UNORM_S8_UINT: return rhi::Format::D24UnormS8Uint;
        default: return rhi::Format::Undefined;
    }
}

VkImageAspectFlags aspectMask(rhi::Format format) {
    if (!rhi::isDepthFormat(format)) {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    return rhi::hasStencil(format) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                   : VK_IMAGE_ASPECT_DEPTH_BIT;
}

VkBufferUsageFlags toVkBufferUsage(rhi::BufferUsage usage) {
    using rhi::BufferUsage;
    VkBufferUsageFlags flags = 0;
    if (hasFlag(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasFlag(usage, BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkImageUsageFlags toVkImageUsage(rhi::TextureUsage usage) {
    using rhi::TextureUsage;
    VkImageUsageFlags flags = 0;
    if (hasFlag(usage, TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasFlag(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasFlag(usage, TextureUsage::DepthStencilAttachment)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (hasFlag(usage, TextureUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasFlag(usage, TextureUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkShaderStageFlags toVkShaderStages(rhi::ShaderStage stages) {
    using rhi::ShaderStage;
    VkShaderStageFlags flags = 0;
    if (hasFlag(stages, ShaderStage::Vertex)) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasFlag(stages, ShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (hasFlag(stages, ShaderStage::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}

VkFilter toVkFilter(rhi::Filter filter) {
    return filter == rhi::Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode toVkAddressMode(rhi::AddressMode mode) {
    switch (mode) {
        case rhi::AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case rhi::AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case rhi::AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

VkFormat toVkVertexFormat(rhi::VertexFormat format) {
    switch (format) {
        case rhi::VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
        case rhi::VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case rhi::VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case rhi::VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case rhi::VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology toVkTopology(rhi::PrimitiveTopology topology) {
    switch (topology) {
        case rhi::PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case rhi::PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case rhi::PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case rhi::PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVkCullMode(rhi::CullMode mode) {
    switch (mode) {
        case rhi::CullMode::None: return VK_CULL_MODE_NONE;
        case rhi::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case rhi::CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkFrontFace toVkFrontFace(rhi::FrontFace face) {
    return face == rhi::FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE
                                             : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

VkCompareOp toVkCompareOp(rhi::CompareOp op) {
    switch (op) {
        case rhi::CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case rhi::CompareOp::Less: return VK_COMPARE_OP_LESS;
        case rhi::CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case rhi::CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case rhi::CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case rhi::CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case rhi::CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case rhi::CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkAttachmentLoadOp toVkLoadOp(rhi::LoadOp op) {
    switch (op) {
        case rhi::LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
        case rhi::LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case rhi::LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

VkAttachmentStoreOp toVkStoreOp(rhi::StoreOp op) {
    return op == rhi::StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                     : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

VkDescriptorType toVkDescriptorType(rhi::BindingType type) {
    switch (type) {
        case rhi::BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case rhi::BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case rhi::BindingType::CombinedTextureSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

}  // namespace bdex::vulkan
