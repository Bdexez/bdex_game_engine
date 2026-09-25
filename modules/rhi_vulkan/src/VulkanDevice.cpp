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

}  // namespace bdex::vulkan
