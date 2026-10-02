// Swapchain half of vulkan::Device: surface, images, acquisition, resizing.

#include "VulkanDevice.hpp"

#include <algorithm>

namespace bdex::vulkan {
namespace {

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    // sRGB targets first: shaders output linear color and the hardware encodes.
    constexpr VkFormat preferred[] = {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    for (const VkFormat wanted : preferred) {
        for (const VkSurfaceFormatKHR& format : formats) {
            if (format.format == wanted &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
    }
    return {VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (!vsync) {
        for (const VkPresentModeKHR wanted :
             {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
                return wanted;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;  // always available
}

}  // namespace

Result<rhi::SwapchainHandle> Device::createSwapchain(const rhi::SwapchainDesc& desc) {
    if (desc.window == nullptr) {
        return fail("a swapchain needs a window");
    }
    if (!presentationEnabled_) {
        return fail("the Vulkan device was created without presentation support (headless)");
    }

    Swapchain swapchain;
    swapchain.window = desc.window;
    swapchain.vsync = desc.vsync;

    const auto surface = desc.window->createVulkanSurface(instance_);
    if (!surface) {
        return std::unexpected(surface.error());
    }
    // VkSurfaceKHR is a pointer or a 64-bit integer depending on the platform.
    swapchain.surface = (VkSurfaceKHR)*surface;  // NOLINT(google-readability-casting)

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queueFamily_, swapchain.surface,
                                         &supported);
    if (supported != VK_TRUE) {
        vkDestroySurfaceKHR(instance_, swapchain.surface, nullptr);
        return fail("the selected GPU cannot present to this window");
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& semaphore : swapchain.imageAvailable) {
        BDEX_VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore));
    }

    if (const auto built = buildSwapchain(swapchain); !built) {
        destroySwapchainObjects(swapchain);
        return std::unexpected(built.error());
    }
    return swapchains_.insert(std::move(swapchain));
}

Result<> Device::buildSwapchain(Swapchain& swapchain) {
    VkSurfaceCapabilitiesKHR capabilities{};
    if (const VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            physicalDevice_, swapchain.surface, &capabilities);
        result != VK_SUCCESS) {
        return fail("cannot query the surface: {}", resultString(result));
    }

    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, swapchain.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, swapchain.surface, &formatCount,
                                         formats.data());
    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    if (surfaceFormat.format == VK_FORMAT_UNDEFINED) {
        return fail("the surface offers no supported 8-bit sRGB/UNORM format");
    }

    u32 modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, swapchain.surface, &modeCount,
                                              nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, swapchain.surface, &modeCount,
                                              modes.data());

    // Some window systems (Wayland) leave the extent up to the application.
    const Extent2D windowSize = swapchain.window->framebufferSize();
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(windowSize.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(windowSize.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return fail("the window has no drawable area");
    }

    u32 imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & compositeAlpha) == 0) {
        for (const VkCompositeAlphaFlagBitsKHR candidate :
             {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if ((capabilities.supportedCompositeAlpha & candidate) != 0) {
                compositeAlpha = candidate;
                break;
            }
        }
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = swapchain.surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = compositeAlpha;
    swapchainInfo.presentMode = choosePresentMode(modes, swapchain.vsync);
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = swapchain.handle;

    VkSwapchainKHR newHandle = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateSwapchainKHR(device_, &swapchainInfo, nullptr, &newHandle);
        result != VK_SUCCESS) {
        return fail("vkCreateSwapchainKHR failed: {}", resultString(result));
    }

    // The previous generation is retired: drop its images and handle.
    releaseSwapchainImages(swapchain);
    if (swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain.handle, nullptr);
    }
    swapchain.handle = newHandle;
    swapchain.format = fromVkFormat(surfaceFormat.format);
    swapchain.extent = extent;
    swapchain.windowSize = windowSize;
    swapchain.outOfDate = false;
    swapchain.acquired = false;

    vkGetSwapchainImagesKHR(device_, swapchain.handle, &imageCount, nullptr);
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain.handle, &imageCount, images.data());

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (u32 i = 0; i < imageCount; ++i) {
        Texture texture;
        texture.image = images[i];
        texture.format = surfaceFormat.format;
        texture.swapchainImage = true;
        texture.desc.width = extent.width;
        texture.desc.height = extent.height;
        texture.desc.format = swapchain.format;
        texture.desc.usage = rhi::TextureUsage::ColorAttachment;
        texture.desc.debugName = "swapchain image";

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        BDEX_VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &texture.view));
        swapchain.images.push_back(textures_.insert(std::move(texture)));

        VkSemaphore semaphore = VK_NULL_HANDLE;
        BDEX_VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore));
        swapchain.renderFinished.push_back(semaphore);
    }

    log::debug("vulkan", "swapchain: {}x{}, {} images, {}", extent.width, extent.height,
               imageCount, rhi::formatName(swapchain.format));
    return {};
}

void Device::releaseSwapchainImages(Swapchain& swapchain) {
    // Callers guarantee the GPU is idle (vkDeviceWaitIdle) before rebuilding
    // or destroying a swapchain, so views and semaphores can go immediately.
    for (const rhi::TextureHandle handle : swapchain.images) {
        if (const auto texture = textures_.take(handle)) {
            vkDestroyImageView(device_, texture->view, nullptr);
        }
    }
    swapchain.images.clear();
    for (const VkSemaphore semaphore : swapchain.renderFinished) {
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    swapchain.renderFinished.clear();
}

void Device::destroySwapchainObjects(Swapchain& swapchain) {
    releaseSwapchainImages(swapchain);
    for (VkSemaphore& semaphore : swapchain.imageAvailable) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    if (swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain.handle, nullptr);
        swapchain.handle = VK_NULL_HANDLE;
    }
    if (swapchain.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, swapchain.surface, nullptr);
        swapchain.surface = VK_NULL_HANDLE;
    }
}

void Device::destroySwapchain(rhi::SwapchainHandle handle) {
    if (!swapchains_.contains(handle)) {
        return;
    }
    BDEX_ASSERT(!inFrame_, "destroySwapchain must not be called while recording a frame");
    vkDeviceWaitIdle(device_);
    auto swapchain = swapchains_.take(handle);
    destroySwapchainObjects(*swapchain);
}

rhi::Format Device::swapchainFormat(rhi::SwapchainHandle handle) const {
    const Swapchain* swapchain = swapchains_.get(handle);
    return swapchain != nullptr ? swapchain->format : rhi::Format::Undefined;
}

rhi::TextureHandle Device::acquireSwapchainTexture(rhi::SwapchainHandle handle) {
    BDEX_ASSERT(inFrame_, "acquireSwapchainTexture outside of a frame");
    Swapchain* swapchain = swapchains_.get(handle);
    if (swapchain == nullptr) {
        return {};
    }
    if (swapchain->acquired) {
        return swapchain->images[swapchain->imageIndex];
    }

    const Extent2D windowSize = swapchain->window->framebufferSize();
    if (windowSize.width == 0 || windowSize.height == 0) {
        return {};  // minimized
    }

    const auto rebuild = [&]() -> bool {
        // Nothing recorded this frame refers to the swapchain yet (it has not
        // been acquired), so only previous frames need to drain.
        vkDeviceWaitIdle(device_);
        if (const auto built = buildSwapchain(*swapchain); !built) {
            log::warn("vulkan", "swapchain rebuild postponed: {}", built.error().message);
            return false;
        }
        return true;
    };

    if ((swapchain->outOfDate || windowSize != swapchain->windowSize) && !rebuild()) {
        return {};
    }

    // At most one retry: an out-of-date result right after a rebuild means
    // the window is still being resized; try again next frame.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const VkResult result =
            vkAcquireNextImageKHR(device_, swapchain->handle, UINT64_MAX,
                                  swapchain->imageAvailable[frameSlot()], VK_NULL_HANDLE,
                                  &swapchain->imageIndex);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            if (result == VK_SUBOPTIMAL_KHR) {
                swapchain->outOfDate = true;  // still usable; rebuild next frame
            }
            swapchain->acquired = true;
            acquired_.push_back(handle);
            const rhi::TextureHandle image = swapchain->images[swapchain->imageIndex];
            // The previous contents are not preserved across presents.
            textures_.get(image)->layout = VK_IMAGE_LAYOUT_UNDEFINED;
            return image;
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR) {
            log::error("vulkan", "vkAcquireNextImageKHR failed: {}", resultString(result));
            return {};
        }
        if (!rebuild()) {
            return {};
        }
    }
    return {};
}

}  // namespace bdex::vulkan
