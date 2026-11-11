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
            return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT};
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

    u32 availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, availableExtensions.data());
    const auto isAvailable = [&availableExtensions](const char* name) {
        return std::any_of(availableExtensions.begin(), availableExtensions.end(),
                           [name](const auto& e) { return std::strcmp(e.extensionName, name) == 0; });
    };

    std::vector<const char*> extensions;
    for (const std::string& extension : config.instanceExtensions) {
        extensions.push_back(extension.c_str());
    }
    // Prerequisite of the modern presentation extensions (present id/timing,
    // surface maintenance...). The engine does not use them yet, but
    // presentation layers stacked on top of the application (frame
    // generation, latency tools) enable them on its behalf and expect the
    // instance to be ready for it.
    if (config.presentation &&
        isAvailable(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
        std::none_of(extensions.begin(), extensions.end(), [](const char* name) {
            return std::strcmp(name, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) == 0;
        })) {
        extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    }

    std::vector<const char*> layers;
    if (config.validation) {
        u32 layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        const bool hasLayer =
            std::any_of(availableLayers.begin(), availableLayers.end(), [](const auto& l) {
                return std::strcmp(l.layerName, kValidationLayer) == 0;
            });

        if (hasLayer) {
            layers.push_back(kValidationLayer);
            validationEnabled_ = true;
        } else {
            log::warn("vulkan", "validation requested but {} is not installed", kValidationLayer);
        }
        if (isAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
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

    // Optional features: enabled when the hardware has them.
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);
    if (supported.samplerAnisotropy == VK_TRUE) {
        features.features.samplerAnisotropy = VK_TRUE;
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        maxSamplerAnisotropy_ = properties.limits.maxSamplerAnisotropy;
    }

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
    barrier.subresourceRange = {aspectMask(texture.desc.format), 0, VK_REMAINING_MIP_LEVELS, 0, 1};

    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);

    texture.layout = layout;
}

bool Device::supportsMipGeneration(VkFormat format) const {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                              VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

void Device::generateMipmaps(VkCommandBuffer commandBuffer, Texture& texture) {
    // The tracked layout describes the whole image, so individual levels are
    // moved with local barriers and brought back to a single common layout
    // (TRANSFER_SRC) before returning.
    const auto moveLevel = [&](u32 level, VkImageLayout from, VkImageLayout to) {
        const LayoutSync source = layoutSync(from);
        const LayoutSync destination = layoutSync(to);
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = source.stage;
        barrier.srcAccessMask = source.access;
        barrier.dstStageMask = destination.stage;
        barrier.dstAccessMask = destination.access;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};

        VkDependencyInfo dependency{};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    };

    i32 width = static_cast<i32>(texture.desc.width);
    i32 height = static_cast<i32>(texture.desc.height);
    for (u32 level = 1; level < texture.desc.mipLevels; ++level) {
        moveLevel(level - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        const i32 nextWidth = std::max(width / 2, 1);
        const i32 nextHeight = std::max(height / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {width, height, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        vkCmdBlitImage(commandBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
        width = nextWidth;
        height = nextHeight;
    }
    moveLevel(texture.desc.mipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    texture.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
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

rhi::TextureHandle Device::createTexture(const rhi::TextureDesc& desc) {
    BDEX_ASSERT(desc.width > 0 && desc.height > 0, "cannot create an empty texture");
    BDEX_ASSERT(desc.format != rhi::Format::Undefined, "texture format is undefined");

    Texture texture;
    texture.desc = desc;
    texture.format = toVkFormat(desc.format);

    // Resolve the mip count: 0 means "full chain", and anything above one
    // level needs blit support for the format.
    const bool renderTarget = hasFlag(desc.usage, rhi::TextureUsage::ColorAttachment) ||
                              hasFlag(desc.usage, rhi::TextureUsage::DepthStencilAttachment);
    const u32 fullChain = rhi::fullMipChainLength(desc.width, desc.height);
    u32 mipLevels = desc.mipLevels == 0 ? fullChain : std::min(desc.mipLevels, fullChain);
    BDEX_ASSERT(!renderTarget || mipLevels == 1, "render targets must have a single mip level");
    if (mipLevels > 1 && !supportsMipGeneration(texture.format)) {
        log::warn("vulkan", "{} cannot be mipmapped on this device", rhi::formatName(desc.format));
        mipLevels = 1;
    }
    texture.desc.mipLevels = mipLevels;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = texture.format;
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (hasFlag(desc.usage, rhi::TextureUsage::Sampled)) {
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;  // writeTexture
    }
    if (mipLevels > 1) {
        // Each level is blitted from the previous one.
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    BDEX_VK_CHECK(vmaCreateImage(allocator_, &imageInfo, &allocationInfo, &texture.image,
                                 &texture.allocation, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = texture.format;
    viewInfo.subresourceRange = {aspectMask(desc.format), 0, mipLevels, 0, 1};
    BDEX_VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &texture.view));

    setDebugName(VK_OBJECT_TYPE_IMAGE, handleToU64(texture.image), desc.debugName);
    setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, handleToU64(texture.view), desc.debugName);
    return textures_.insert(std::move(texture));
}

void Device::destroyTexture(rhi::TextureHandle handle) {
    const Texture* existing = textures_.get(handle);
    if (existing == nullptr || existing->swapchainImage) {
        return;  // swapchain images belong to their swapchain
    }
    const auto texture = textures_.take(handle);
    defer([device = device_, allocator = allocator_, image = texture->image, view = texture->view,
           allocation = texture->allocation] {
        vkDestroyImageView(device, view, nullptr);
        vmaDestroyImage(allocator, image, allocation);
    });
}

void Device::writeTexture(rhi::TextureHandle handle, std::span<const std::byte> pixels) {
    {
        const Texture* texture = textures_.get(handle);
        BDEX_ASSERT(texture != nullptr, "writeTexture on an invalid texture");
        BDEX_ASSERT(pixels.size() == static_cast<usize>(texture->desc.width) *
                                         texture->desc.height *
                                         rhi::bytesPerPixel(texture->desc.format),
                    "writeTexture size mismatch");
    }

    rhi::BufferDesc stagingDesc;
    stagingDesc.size = pixels.size();
    stagingDesc.usage = rhi::BufferUsage::TransferSrc;
    stagingDesc.memory = rhi::MemoryUsage::CpuToGpu;
    const rhi::BufferHandle stagingHandle = createBuffer(stagingDesc);
    const Buffer* staging = buffers_.get(stagingHandle);
    std::memcpy(staging->mapped, pixels.data(), pixels.size());
    BDEX_VK_CHECK(vmaFlushAllocation(allocator_, staging->allocation, 0, pixels.size()));

    Texture* texture = textures_.get(handle);
    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        transition(commandBuffer, *texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = {aspectMask(texture->desc.format), 0, 0, 1};
        region.imageExtent = {texture->desc.width, texture->desc.height, 1};
        vkCmdCopyBufferToImage(commandBuffer, staging->handle, texture->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        if (texture->desc.mipLevels > 1) {
            generateMipmaps(commandBuffer, *texture);
        }
        if (hasFlag(texture->desc.usage, rhi::TextureUsage::Sampled)) {
            transition(commandBuffer, *texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });
    destroyBuffer(stagingHandle);
}

std::vector<u8> Device::readTexture(rhi::TextureHandle handle) {
    BDEX_ASSERT(!inFrame_, "readTexture must not be called while recording a frame");
    {
        const Texture* texture = textures_.get(handle);
        BDEX_ASSERT(texture != nullptr, "readTexture on an invalid texture");
        BDEX_ASSERT(hasFlag(texture->desc.usage, rhi::TextureUsage::TransferSrc),
                    "readTexture requires TextureUsage::TransferSrc");
        BDEX_ASSERT(!rhi::isDepthFormat(texture->desc.format) &&
                        rhi::bytesPerPixel(texture->desc.format) == 4,
                    "readTexture supports 8-bit color formats only");
    }

    // The frames that rendered to the texture must have finished.
    waitIdle();

    const rhi::TextureDesc desc = textures_.get(handle)->desc;
    const usize byteCount = static_cast<usize>(desc.width) * desc.height * 4;

    rhi::BufferDesc readbackDesc;
    readbackDesc.size = byteCount;
    readbackDesc.usage = rhi::BufferUsage::TransferDst;
    readbackDesc.memory = rhi::MemoryUsage::GpuToCpu;
    const rhi::BufferHandle readbackHandle = createBuffer(readbackDesc);
    const Buffer* readback = buffers_.get(readbackHandle);

    Texture* texture = textures_.get(handle);
    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        const VkImageLayout previous = texture->layout;
        transition(commandBuffer, *texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {desc.width, desc.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer, texture->image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->handle, 1, &region);

        if (previous != VK_IMAGE_LAYOUT_UNDEFINED) {
            transition(commandBuffer, *texture, previous);
        }
    });

    BDEX_VK_CHECK(vmaInvalidateAllocation(allocator_, readback->allocation, 0, byteCount));
    std::vector<u8> pixels(byteCount);
    std::memcpy(pixels.data(), readback->mapped, byteCount);
    destroyBuffer(readbackHandle);

    // Always hand back RGBA, whatever the channel order of the texture.
    if (desc.format == rhi::Format::BGRA8Unorm || desc.format == rhi::Format::BGRA8Srgb) {
        for (usize i = 0; i < byteCount; i += 4) {
            std::swap(pixels[i], pixels[i + 2]);
        }
    }
    return pixels;
}

rhi::TextureDesc Device::textureDesc(rhi::TextureHandle handle) const {
    if (const Texture* texture = textures_.get(handle)) {
        return texture->desc;
    }
    rhi::TextureDesc invalid;
    invalid.width = 0;
    invalid.height = 0;
    invalid.format = rhi::Format::Undefined;
    invalid.usage = rhi::TextureUsage::None;
    return invalid;
}

rhi::SamplerHandle Device::createSampler(const rhi::SamplerDesc& desc) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.minFilter = toVkFilter(desc.minFilter);
    samplerInfo.magFilter = toVkFilter(desc.magFilter);
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = toVkAddressMode(desc.addressMode);
    samplerInfo.addressModeV = toVkAddressMode(desc.addressMode);
    samplerInfo.addressModeW = toVkAddressMode(desc.addressMode);
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (desc.maxAnisotropy > 1.0f && maxSamplerAnisotropy_ > 1.0f) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(desc.maxAnisotropy, maxSamplerAnisotropy_);
    }

    Sampler sampler;
    BDEX_VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler.handle));
    setDebugName(VK_OBJECT_TYPE_SAMPLER, handleToU64(sampler.handle), desc.debugName);
    return samplers_.insert(sampler);
}

void Device::destroySampler(rhi::SamplerHandle handle) {
    if (const auto sampler = samplers_.take(handle)) {
        defer([device = device_, vkSampler = sampler->handle] {
            vkDestroySampler(device, vkSampler, nullptr);
        });
    }
}

// =============================================================================
// Shaders, bind groups, pipelines
// =============================================================================

rhi::ShaderHandle Device::createShader(const rhi::ShaderDesc& desc) {
    BDEX_ASSERT(!desc.spirv.empty(), "shader has no SPIR-V code");

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = desc.spirv.size_bytes();
    moduleInfo.pCode = desc.spirv.data();

    Shader shader;
    shader.stage = desc.stage;
    shader.entryPoint = desc.entryPoint;
    BDEX_VK_CHECK(vkCreateShaderModule(device_, &moduleInfo, nullptr, &shader.module));
    setDebugName(VK_OBJECT_TYPE_SHADER_MODULE, handleToU64(shader.module), desc.debugName);
    return shaders_.insert(std::move(shader));
}

void Device::destroyShader(rhi::ShaderHandle handle) {
    if (const auto shader = shaders_.take(handle)) {
        // Pipelines keep no reference to the module once created.
        vkDestroyShaderModule(device_, shader->module, nullptr);
    }
}

rhi::BindGroupLayoutHandle Device::createBindGroupLayout(const rhi::BindGroupLayoutDesc& desc) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());
    for (const rhi::BindGroupLayoutEntry& entry : desc.entries) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = entry.binding;
        binding.descriptorType = toVkDescriptorType(entry.type);
        binding.descriptorCount = 1;
        binding.stageFlags = toVkShaderStages(entry.stages);
        bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    BindGroupLayout layout;
    layout.desc = desc;
    BDEX_VK_CHECK(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout.handle));
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, handleToU64(layout.handle), desc.debugName);
    return bindGroupLayouts_.insert(std::move(layout));
}

void Device::destroyBindGroupLayout(rhi::BindGroupLayoutHandle handle) {
    if (const auto layout = bindGroupLayouts_.take(handle)) {
        defer([device = device_, vkLayout = layout->handle] {
            vkDestroyDescriptorSetLayout(device, vkLayout, nullptr);
        });
    }
}

rhi::BindGroupHandle Device::createBindGroup(const rhi::BindGroupDesc& desc) {
    const BindGroupLayout* layout = bindGroupLayouts_.get(desc.layout);
    BDEX_ASSERT(layout != nullptr, "bind group created with an invalid layout");

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout->handle;

    BindGroup group;
    BDEX_VK_CHECK(vkAllocateDescriptorSets(device_, &allocateInfo, &group.handle));
    setDebugName(VK_OBJECT_TYPE_DESCRIPTOR_SET, handleToU64(group.handle), desc.debugName);

    // Reserve up front: the write structs point into these arrays.
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    writes.reserve(desc.entries.size());

    for (const rhi::BindGroupEntry& entry : desc.entries) {
        const auto declared =
            std::find_if(layout->desc.entries.begin(), layout->desc.entries.end(),
                         [&](const auto& candidate) { return candidate.binding == entry.binding; });
        BDEX_ASSERT(declared != layout->desc.entries.end(),
                    "bind group entry is not declared in its layout");

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = group.handle;
        write.dstBinding = entry.binding;
        write.descriptorCount = 1;
        write.descriptorType = toVkDescriptorType(declared->type);

        if (declared->type == rhi::BindingType::CombinedTextureSampler) {
            const Texture* texture = textures_.get(entry.texture);
            const Sampler* sampler = samplers_.get(entry.sampler);
            BDEX_ASSERT(texture != nullptr && sampler != nullptr,
                        "bind group entry needs a valid texture and sampler");
            imageInfos.push_back(
                {sampler->handle, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            write.pImageInfo = &imageInfos.back();
        } else {
            const Buffer* buffer = buffers_.get(entry.buffer);
            BDEX_ASSERT(buffer != nullptr, "bind group entry needs a valid buffer");
            bufferInfos.push_back({buffer->handle, entry.offset,
                                   entry.size == rhi::kWholeSize ? VK_WHOLE_SIZE : entry.size});
            write.pBufferInfo = &bufferInfos.back();
        }
        writes.push_back(write);
    }
    vkUpdateDescriptorSets(device_, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    return bindGroups_.insert(group);
}

void Device::destroyBindGroup(rhi::BindGroupHandle handle) {
    if (const auto group = bindGroups_.take(handle)) {
        defer([device = device_, pool = descriptorPool_, set = group->handle] {
            vkFreeDescriptorSets(device, pool, 1, &set);
        });
    }
}

rhi::PipelineHandle Device::createGraphicsPipeline(const rhi::GraphicsPipelineDesc& desc) {
    const Shader* vertexShader = shaders_.get(desc.vertexShader);
    const Shader* fragmentShader = shaders_.get(desc.fragmentShader);
    BDEX_ASSERT(vertexShader != nullptr && fragmentShader != nullptr,
                "pipeline created with invalid shaders");

    Pipeline pipeline;
    pipeline.pushConstantStages = toVkShaderStages(desc.pushConstantStages);

    // --- Layout ------------------------------------------------------------------
    std::vector<VkDescriptorSetLayout> setLayouts;
    for (const rhi::BindGroupLayoutHandle handle : desc.bindGroupLayouts) {
        const BindGroupLayout* layout = bindGroupLayouts_.get(handle);
        BDEX_ASSERT(layout != nullptr, "pipeline created with an invalid bind group layout");
        setLayouts.push_back(layout->handle);
    }
    const VkPushConstantRange pushConstantRange{pipeline.pushConstantStages, 0,
                                                desc.pushConstantSize};
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    if (desc.pushConstantSize > 0) {
        BDEX_ASSERT(pipeline.pushConstantStages != 0, "push constants need at least one stage");
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
    } else {
        pipeline.pushConstantStages = 0;
    }
    BDEX_VK_CHECK(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipeline.layout));

    // --- Stages ------------------------------------------------------------------
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader->module;
    stages[0].pName = vertexShader->entryPoint.c_str();
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader->module;
    stages[1].pName = fragmentShader->entryPoint.c_str();

    // --- Fixed-function state ------------------------------------------------------
    const VkVertexInputBindingDescription vertexBinding{0, desc.vertexLayout.stride,
                                                        VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    for (const rhi::VertexAttribute& attribute : desc.vertexLayout.attributes) {
        vertexAttributes.push_back(
            {attribute.location, 0, toVkVertexFormat(attribute.format), attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertexLayout.stride > 0) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVkTopology(desc.topology);

    // Viewport and scissor are dynamic: pipelines survive window resizes.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = toVkCullMode(desc.cullMode);
    rasterization.frontFace = toVkFrontFace(desc.frontFace);
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    const bool hasDepth = desc.depthFormat != rhi::Format::Undefined;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = hasDepth && desc.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = hasDepth && desc.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depthCompare);

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch (desc.blend) {
        case rhi::BlendMode::Opaque: break;
        case rhi::BlendMode::AlphaBlend:
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case rhi::BlendMode::Additive:
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
    const std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        desc.colorFormats.size(), blendAttachment);
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<u32>(blendAttachments.size());
    colorBlend.pAttachments = blendAttachments.data();

    const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                         VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Dynamic rendering: attachment formats replace the render pass object.
    std::vector<VkFormat> colorFormats;
    for (const rhi::Format format : desc.colorFormats) {
        colorFormats.push_back(toVkFormat(format));
    }
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = static_cast<u32>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = toVkFormat(desc.depthFormat);
    renderingInfo.stencilAttachmentFormat =
        rhi::hasStencil(desc.depthFormat) ? toVkFormat(desc.depthFormat) : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<u32>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline.layout;
    BDEX_VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                            &pipeline.handle));

    setDebugName(VK_OBJECT_TYPE_PIPELINE, handleToU64(pipeline.handle), desc.debugName);
    return pipelines_.insert(pipeline);
}

void Device::destroyPipeline(rhi::PipelineHandle handle) {
    if (const auto pipeline = pipelines_.take(handle)) {
        defer([device = device_, vkPipeline = pipeline->handle, layout = pipeline->layout] {
            vkDestroyPipeline(device, vkPipeline, nullptr);
            vkDestroyPipelineLayout(device, layout, nullptr);
        });
    }
}

// =============================================================================
// Frame
// =============================================================================

rhi::ICommandList& Device::beginFrame() {
    BDEX_ASSERT(!inFrame_, "beginFrame called twice");
    FrameData& frame = frames_[frameSlot()];

    // This slot was last used by frame N - kFramesInFlight: once its fence is
    // signaled, everything up to that frame has left the GPU.
    BDEX_VK_CHECK(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX));
    BDEX_VK_CHECK(vkResetFences(device_, 1, &frame.fence));
    if (frameNumber_ >= kFramesInFlight) {
        flushDeferred(frameNumber_ - kFramesInFlight);
    }

    BDEX_VK_CHECK(vkResetCommandPool(device_, frame.pool, 0));
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    BDEX_VK_CHECK(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo));

    commandList_.begin(frame.commandBuffer);
    inFrame_ = true;
    return commandList_;
}

void Device::endFrame() {
    BDEX_ASSERT(inFrame_, "endFrame without beginFrame");
    FrameData& frame = frames_[frameSlot()];

    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkSemaphoreSubmitInfo> signals;
    for (const rhi::SwapchainHandle handle : acquired_) {
        Swapchain* swapchain = swapchains_.get(handle);
        Texture* image = textures_.get(swapchain->images[swapchain->imageIndex]);
        transition(frame.commandBuffer, *image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VkSemaphoreSubmitInfo wait{};
        wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        wait.semaphore = swapchain->imageAvailable[frameSlot()];
        // Block the whole command buffer until the image is ours: layout
        // transitions of the swapchain image may be the very first command.
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waits.push_back(wait);

        VkSemaphoreSubmitInfo signal{};
        signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signal.semaphore = swapchain->renderFinished[swapchain->imageIndex];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals.push_back(signal);
    }

    lastStats_ = commandList_.stats();
    commandList_.end();
    BDEX_VK_CHECK(vkEndCommandBuffer(frame.commandBuffer));

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = frame.commandBuffer;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = static_cast<u32>(waits.size());
    submitInfo.pWaitSemaphoreInfos = waits.data();
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = static_cast<u32>(signals.size());
    submitInfo.pSignalSemaphoreInfos = signals.data();
    BDEX_VK_CHECK(vkQueueSubmit2(queue_, 1, &submitInfo, frame.fence));

    for (const rhi::SwapchainHandle handle : acquired_) {
        Swapchain* swapchain = swapchains_.get(handle);
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &swapchain->renderFinished[swapchain->imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain->handle;
        presentInfo.pImageIndices = &swapchain->imageIndex;
        const VkResult result = vkQueuePresentKHR(queue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            swapchain->outOfDate = true;  // rebuilt on the next acquire
        } else if (result != VK_SUCCESS) {
            log::error("vulkan", "vkQueuePresentKHR failed: {}", resultString(result));
        }
        swapchain->acquired = false;
    }
    acquired_.clear();

    inFrame_ = false;
    ++frameNumber_;
}

void Device::waitIdle() {
    BDEX_VK_CHECK(vkDeviceWaitIdle(device_));
}

}  // namespace bdex::vulkan
