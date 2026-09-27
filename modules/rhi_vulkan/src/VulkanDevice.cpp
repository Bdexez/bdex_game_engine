#include "VulkanDevice.hpp"

#include <algorithm>
#include <cstring>
#include <format>

namespace bdex::vulkan {
namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

/// Pipeline stage and access mask that match the use of an image in `layout`.
struct LayoutSync {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

LayoutSync layoutSync(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
        default:
            BDEX_ASSERT(false, "unsupported image layout");
            return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE};
    }
}

std::string versionString(u32 version) {
    return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                       VK_API_VERSION_PATCH(version));
}

template <class T>
u64 handleToU64(T handle) {
    // Vulkan handles are pointers or 64-bit integers depending on the type
    // and the platform; a C-style cast covers both.
    return (u64)handle;  // NOLINT(google-readability-casting)
}

}  // namespace

// =============================================================================
// Creation / destruction
// =============================================================================

Result<std::unique_ptr<Device>> Device::create(const DeviceConfig& config) {
    std::unique_ptr<Device> device(new Device());
    if (const auto result = device->initialize(config); !result) {
        return std::unexpected(result.error());
    }
    return device;
}

Result<> Device::initialize(const DeviceConfig& config) {
    if (const auto result = createInstance(config); !result) {
        return result;
    }
    if (const auto result = selectPhysicalDevice(config); !result) {
        return result;
    }
    if (const auto result = createLogicalDevice(config); !result) {
        return result;
    }

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (vmaCreateAllocator(&allocatorInfo, &allocator_) != VK_SUCCESS) {
        return fail("cannot create the Vulkan memory allocator");
    }

    createFrameResources();

    // One pool for every bind group. Sets are freed individually.
    const std::array<VkDescriptorPoolSize, 3> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2048},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 4096;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    BDEX_VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_));

    log::info("vulkan", "device: {} (Vulkan {}, validation {})", adapterName_,
              versionString(adapterApiVersion_), validationEnabled_ ? "on" : "off");
    return {};
}

Result<> Device::createInstance(const DeviceConfig& config) {
    u32 instanceVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&instanceVersion);
    if (instanceVersion < VK_API_VERSION_1_3) {
        return fail("Vulkan 1.3 is required, the installed loader only supports {}",
                    versionString(instanceVersion));
    }

    std::vector<const char*> extensions;
    for (const std::string& extension : config.instanceExtensions) {
        extensions.push_back(extension.c_str());
    }
    std::vector<const char*> layers;

    if (config.validation) {
        u32 layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());
        const bool hasLayer = std::any_of(available.begin(), available.end(), [](const auto& l) {
            return std::strcmp(l.layerName, kValidationLayer) == 0;
        });

        u32 extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                               availableExtensions.data());
        const bool hasDebugUtils =
            std::any_of(availableExtensions.begin(), availableExtensions.end(), [](const auto& e) {
                return std::strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
            });

        if (hasLayer) {
            layers.push_back(kValidationLayer);
            validationEnabled_ = true;
        } else {
            log::warn("vulkan", "validation requested but {} is not installed", kValidationLayer);
        }
        if (hasDebugUtils) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            debugUtilsEnabled_ = true;
        }
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = config.applicationName.c_str();
    applicationInfo.pEngineName = "bdex";
    applicationInfo.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
    messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = &Device::debugCallback;
    messengerInfo.pUserData = this;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    instanceInfo.enabledLayerCount = static_cast<u32>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    if (debugUtilsEnabled_) {
        // Also reports problems during instance creation and destruction.
        instanceInfo.pNext = &messengerInfo;
    }

    if (const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance_);
        result != VK_SUCCESS) {
        return fail("vkCreateInstance failed: {}", resultString(result));
    }

    if (debugUtilsEnabled_) {
        const auto load = [this](const char* name) {
            return vkGetInstanceProcAddr(instance_, name);
        };
        createMessenger_ = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            load("vkCreateDebugUtilsMessengerEXT"));
        destroyMessenger_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            load("vkDestroyDebugUtilsMessengerEXT"));
        setObjectName_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            load("vkSetDebugUtilsObjectNameEXT"));
        cmdBeginLabel_ = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            load("vkCmdBeginDebugUtilsLabelEXT"));
        cmdEndLabel_ =
            reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(load("vkCmdEndDebugUtilsLabelEXT"));
        if (createMessenger_ != nullptr) {
            createMessenger_(instance_, &messengerInfo, nullptr, &messenger_);
        }
    }
    return {};
}

Result<> Device::selectPhysicalDevice(const DeviceConfig& config) {
    u32 count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> candidates(count);
    vkEnumeratePhysicalDevices(instance_, &count, candidates.data());
    if (candidates.empty()) {
        return fail("no Vulkan device found");
    }

    i32 bestScore = -1;
    for (const VkPhysicalDevice candidate : candidates) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) {
            continue;
        }

        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (features13.dynamicRendering != VK_TRUE || features13.synchronization2 != VK_TRUE) {
            continue;
        }

        u32 familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        u32 graphicsFamily = familyCount;
        for (u32 i = 0; i < familyCount; ++i) {
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                graphicsFamily = i;
                break;
            }
        }
        if (graphicsFamily == familyCount) {
            continue;
        }

        if (config.presentation) {
            u32 extensionCount = 0;
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount,
                                                 extensions.data());
            const bool hasSwapchain =
                std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
                    return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                });
            if (!hasSwapchain) {
                continue;
            }
        }

        i32 score = 0;
        switch (properties.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 1000; break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 500; break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 250; break;
            default: score = 100; break;
        }
        if (!config.preferredAdapter.empty() &&
            std::string_view(properties.deviceName).find(config.preferredAdapter) !=
                std::string_view::npos) {
            score += 10'000;
        }

        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = candidate;
            queueFamily_ = graphicsFamily;
            adapterName_ = properties.deviceName;
            adapterApiVersion_ = properties.apiVersion;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        return fail("no suitable Vulkan device: Vulkan 1.3 with dynamic rendering and "
                    "synchronization2 is required");
    }
    return {};
}

Result<> Device::createLogicalDevice(const DeviceConfig& config) {
    const f32 priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;

    std::vector<const char*> extensions;
    if (config.presentation) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        presentationEnabled_ = true;
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.data();

    if (const VkResult result = vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_);
        result != VK_SUCCESS) {
        return fail("vkCreateDevice failed: {}", resultString(result));
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
    return {};
}

void Device::createFrameResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily_;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    const auto allocateCommandBuffer = [this](VkCommandPool pool) {
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = pool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        BDEX_VK_CHECK(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer));
        return commandBuffer;
    };

    for (FrameData& frame : frames_) {
        BDEX_VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &frame.pool));
        frame.commandBuffer = allocateCommandBuffer(frame.pool);
        // Created signaled so the very first beginFrame does not block.
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        BDEX_VK_CHECK(vkCreateFence(device_, &fenceInfo, nullptr, &frame.fence));
    }

    BDEX_VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &immediatePool_));
    immediateCommandBuffer_ = allocateCommandBuffer(immediatePool_);
    fenceInfo.flags = 0;
    BDEX_VK_CHECK(vkCreateFence(device_, &fenceInfo, nullptr, &immediateFence_));
}

Device::~Device() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);

        // Swapchains first: they own textures.
        std::vector<rhi::SwapchainHandle> swapchains;
        swapchains_.forEach([&](rhi::SwapchainHandle handle, Swapchain&) {
            swapchains.push_back(handle);
        });
        for (const rhi::SwapchainHandle handle : swapchains) {
            destroySwapchain(handle);
        }

        // Anything still alive was leaked by its owner; release it and say so.
        const usize leaked = buffers_.size() + textures_.size() + samplers_.size() +
                             shaders_.size() + bindGroupLayouts_.size() + bindGroups_.size() +
                             pipelines_.size();
        if (leaked != 0) {
            log::warn("vulkan", "{} resource(s) were never destroyed", leaked);
        }
        const auto collect = [](auto& pool) {
            std::vector<typename std::remove_reference_t<decltype(pool)>::HandleType> handles;
            pool.forEach([&](auto handle, auto&) { handles.push_back(handle); });
            return handles;
        };
        for (const auto handle : collect(pipelines_)) destroyPipeline(handle);
        for (const auto handle : collect(bindGroups_)) destroyBindGroup(handle);
        for (const auto handle : collect(bindGroupLayouts_)) destroyBindGroupLayout(handle);
        for (const auto handle : collect(shaders_)) destroyShader(handle);
        for (const auto handle : collect(samplers_)) destroySampler(handle);
        for (const auto handle : collect(textures_)) destroyTexture(handle);
        for (const auto handle : collect(buffers_)) destroyBuffer(handle);
        flushAllDeferred();

        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        for (FrameData& frame : frames_) {
            if (frame.fence != VK_NULL_HANDLE) vkDestroyFence(device_, frame.fence, nullptr);
            if (frame.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device_, frame.pool, nullptr);
        }
        if (immediateFence_ != VK_NULL_HANDLE) vkDestroyFence(device_, immediateFence_, nullptr);
        if (immediatePool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, immediatePool_, nullptr);
        }
        if (allocator_ != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator_);
        }
        vkDestroyDevice(device_, nullptr);
    }

    if (instance_ != VK_NULL_HANDLE) {
        if (messenger_ != VK_NULL_HANDLE && destroyMessenger_ != nullptr) {
            destroyMessenger_(instance_, messenger_, nullptr);
        }
        vkDestroyInstance(instance_, nullptr);
    }
}

rhi::DeviceInfo Device::info() const {
    return rhi::DeviceInfo{"vulkan", adapterName_, versionString(adapterApiVersion_),
                           validationEnabled_};
}

VKAPI_ATTR VkBool32 VKAPI_CALL Device::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData) {
    auto* self = static_cast<Device*>(userData);
    const char* message = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        self->validationErrors_.fetch_add(1, std::memory_order_relaxed);
        log::error("vulkan", "{}", message);
    } else {
        log::warn("vulkan", "{}", message);
    }
    return VK_FALSE;
}

void Device::setDebugName(VkObjectType type, u64 handle, const std::string& name) {
    if (setObjectName_ == nullptr || name.empty() || handle == 0) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    setObjectName_(device_, &info);
}

void Device::beginDebugLabel(VkCommandBuffer commandBuffer, std::string_view name) {
    if (cmdBeginLabel_ == nullptr) {
        return;
    }
    const std::string label(name);
    VkDebugUtilsLabelEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    info.pLabelName = label.c_str();
    cmdBeginLabel_(commandBuffer, &info);
}

void Device::endDebugLabel(VkCommandBuffer commandBuffer) {
    if (cmdEndLabel_ != nullptr) {
        cmdEndLabel_(commandBuffer);
    }
}

// =============================================================================
// Deferred release & one-off submissions
// =============================================================================

void Device::defer(std::function<void()> release) {
    // The object may be referenced by the frame being recorded (or, outside of
    // a frame, by the last one submitted - tagging it with the upcoming frame
    // is conservative and keeps a single rule).
    deferred_.push_back({frameNumber_, std::move(release)});
}

void Device::flushDeferred(u64 completedFrame) {
    while (!deferred_.empty() && deferred_.front().frame <= completedFrame) {
        deferred_.front().release();
        deferred_.pop_front();
    }
}

void Device::flushAllDeferred() {
    while (!deferred_.empty()) {
        deferred_.front().release();
        deferred_.pop_front();
    }
}

void Device::immediateSubmit(const std::function<void(VkCommandBuffer)>& record) {
    BDEX_VK_CHECK(vkResetCommandPool(device_, immediatePool_, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BDEX_VK_CHECK(vkBeginCommandBuffer(immediateCommandBuffer_, &beginInfo));
    record(immediateCommandBuffer_);
    BDEX_VK_CHECK(vkEndCommandBuffer(immediateCommandBuffer_));

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = immediateCommandBuffer_;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    BDEX_VK_CHECK(vkQueueSubmit2(queue_, 1, &submitInfo, immediateFence_));
    BDEX_VK_CHECK(vkWaitForFences(device_, 1, &immediateFence_, VK_TRUE, UINT64_MAX));
    BDEX_VK_CHECK(vkResetFences(device_, 1, &immediateFence_));
}

void Device::transition(VkCommandBuffer commandBuffer, Texture& texture, VkImageLayout layout) {
    if (texture.layout == layout) {
        return;
    }
    const LayoutSync source = layoutSync(texture.layout);
    const LayoutSync destination = layoutSync(layout);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = source.stage;
    barrier.srcAccessMask = source.access;
    barrier.dstStageMask = destination.stage;
    barrier.dstAccessMask = destination.access;
    barrier.oldLayout = texture.layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image;
    barrier.subresourceRange = {aspectMask(texture.desc.format), 0, 1, 0, 1};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    texture.layout = layout;
}

// =============================================================================
// Buffers
// =============================================================================

rhi::BufferHandle Device::createBuffer(const rhi::BufferDesc& desc) {
    BDEX_ASSERT(desc.size > 0, "cannot create an empty buffer");

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (desc.memory) {
        case rhi::MemoryUsage::GpuOnly:
            // Filled through a staging copy.
            bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case rhi::MemoryUsage::CpuToGpu:
            allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case rhi::MemoryUsage::GpuToCpu:
            allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                   VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
    }

    Buffer buffer;
    buffer.desc = desc;
    VmaAllocationInfo allocated{};
    BDEX_VK_CHECK(vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo, &buffer.handle,
                                  &buffer.allocation, &allocated));
    buffer.mapped = allocated.pMappedData;
    setDebugName(VK_OBJECT_TYPE_BUFFER, handleToU64(buffer.handle), desc.debugName);
    return buffers_.insert(std::move(buffer));
}

void Device::destroyBuffer(rhi::BufferHandle handle) {
    if (const auto buffer = buffers_.take(handle)) {
        defer([allocator = allocator_, vkBuffer = buffer->handle,
               allocation = buffer->allocation] {
            vmaDestroyBuffer(allocator, vkBuffer, allocation);
        });
    }
}

void Device::writeBuffer(rhi::BufferHandle handle, std::span<const std::byte> data, u64 offset) {
    Buffer* buffer = buffers_.get(handle);
    BDEX_ASSERT(buffer != nullptr, "writeBuffer on an invalid buffer");
    BDEX_ASSERT(offset + data.size() <= buffer->desc.size, "writeBuffer out of bounds");
    if (data.empty()) {
        return;
    }

    if (buffer->mapped != nullptr) {
        std::memcpy(static_cast<std::byte*>(buffer->mapped) + offset, data.data(), data.size());
        BDEX_VK_CHECK(vmaFlushAllocation(allocator_, buffer->allocation, offset, data.size()));
        return;
    }

    // Device-local buffer: go through a temporary host-visible buffer.
    rhi::BufferDesc stagingDesc;
    stagingDesc.size = data.size();
    stagingDesc.usage = rhi::BufferUsage::TransferSrc;
    stagingDesc.memory = rhi::MemoryUsage::CpuToGpu;
    const rhi::BufferHandle stagingHandle = createBuffer(stagingDesc);
    const Buffer* staging = buffers_.get(stagingHandle);
    buffer = buffers_.get(handle);  // the pool may have been reallocated
    std::memcpy(staging->mapped, data.data(), data.size());
    BDEX_VK_CHECK(vmaFlushAllocation(allocator_, staging->allocation, 0, data.size()));

    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        const VkBufferCopy region{0, offset, data.size()};
        vkCmdCopyBuffer(commandBuffer, staging->handle, buffer->handle, 1, &region);
    });
    destroyBuffer(stagingHandle);
}

// =============================================================================
// Textures & samplers
// =============================================================================

}  // namespace bdex::vulkan
