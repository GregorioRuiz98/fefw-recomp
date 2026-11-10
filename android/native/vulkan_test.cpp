// SPDX-License-Identifier: GPL-3.0-or-later
// Real Android surface/swapchain exercise. This does not render game assets.
#include <jni.h>
#include <android/native_window_jni.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
void checked(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(result) + ")");
}
struct Session {
    ANativeWindow* window{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkDevice device{};
    VkSwapchainKHR swapchain{};
    VkCommandPool pool{};
    VkSemaphore acquired{}, rendered{};
    VkFence fence{};
    ~Session() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (rendered) vkDestroySemaphore(device, rendered, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        if (window) ANativeWindow_release(window);
    }
};

std::string render(JNIEnv* env, jobject java_surface) {
    Session s;
    s.window = ANativeWindow_fromSurface(env, java_surface);
    if (!s.window) throw std::runtime_error("Android surface is unavailable");
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "FEFW Android surface test"; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &app; instance.enabledExtensionCount = 2; instance.ppEnabledExtensionNames = extensions;
    checked(vkCreateInstance(&instance, nullptr, &s.instance), "Create Vulkan instance");
    VkAndroidSurfaceCreateInfoKHR android_surface{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    android_surface.window = s.window;
    checked(vkCreateAndroidSurfaceKHR(s.instance, &android_surface, nullptr, &s.surface), "Create Android Vulkan surface");
    uint32_t count = 0;
    checked(vkEnumeratePhysicalDevices(s.instance, &count, nullptr), "Enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    checked(vkEnumeratePhysicalDevices(s.instance, &count, devices.data()), "Read GPUs");
    VkPhysicalDevice physical{};
    uint32_t family = 0;
    for (auto candidate : devices) {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, queues.data());
        for (uint32_t i = 0; i < queue_count; ++i) {
            VkBool32 present = VK_FALSE;
            checked(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, s.surface, &present), "Check presentation queue");
            if (present && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { physical = candidate; family = i; break; }
        }
        if (physical) break;
    }
    if (!physical) throw std::runtime_error("No GPU queue can render and present on this surface");
    const float priority = 1;
    VkDeviceQueueCreateInfo queue_create{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_create.queueFamilyIndex = family; queue_create.queueCount = 1; queue_create.pQueuePriorities = &priority;
    const char* device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create.queueCreateInfoCount = 1; device_create.pQueueCreateInfos = &queue_create;
    device_create.enabledExtensionCount = 1; device_create.ppEnabledExtensionNames = device_extensions;
    checked(vkCreateDevice(physical, &device_create, nullptr, &s.device), "Create Vulkan device");
    VkQueue queue{}; vkGetDeviceQueue(s.device, family, 0, &queue);
    VkSurfaceCapabilitiesKHR caps{};
    checked(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, s.surface, &caps), "Read surface capabilities");
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) throw std::runtime_error("Surface does not allow clear transfers");
    uint32_t format_count = 0;
    checked(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, s.surface, &format_count, nullptr), "Count surface formats");
    if (!format_count) throw std::runtime_error("No surface formats");
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    checked(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, s.surface, &format_count, formats.data()), "Read surface formats");
    VkSurfaceFormatKHR format = formats[0];
    for (auto candidate : formats) if (candidate.format == VK_FORMAT_R8G8B8A8_UNORM) format = candidate;
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(uint32_t(std::max(1, ANativeWindow_getWidth(s.window))), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(uint32_t(std::max(1, ANativeWindow_getHeight(s.window))), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!extent.width || !extent.height) throw std::runtime_error("Surface has zero size");
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (uint32_t bit = 1; bit <= 8; bit <<= 1) if (caps.supportedCompositeAlpha & bit) { alpha = static_cast<VkCompositeAlphaFlagBitsKHR>(bit); break; }
    VkSwapchainCreateInfoKHR swapchain{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain.surface = s.surface; swapchain.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount) swapchain.minImageCount = std::min(swapchain.minImageCount, caps.maxImageCount);
    swapchain.imageFormat = format.format; swapchain.imageColorSpace = format.colorSpace;
    swapchain.imageExtent = extent; swapchain.imageArrayLayers = 1; swapchain.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; swapchain.preTransform = caps.currentTransform;
    swapchain.compositeAlpha = alpha; swapchain.presentMode = VK_PRESENT_MODE_FIFO_KHR; swapchain.clipped = VK_TRUE;
    checked(vkCreateSwapchainKHR(s.device, &swapchain, nullptr, &s.swapchain), "Create swapchain");
    uint32_t image_count = 0;
    checked(vkGetSwapchainImagesKHR(s.device, s.swapchain, &image_count, nullptr), "Count swapchain images");
    std::vector<VkImage> images(image_count);
    checked(vkGetSwapchainImagesKHR(s.device, s.swapchain, &image_count, images.data()), "Read swapchain images");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = family; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    checked(vkCreateCommandPool(s.device, &pool, nullptr, &s.pool), "Create command pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = s.pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
    VkCommandBuffer command{}; checked(vkAllocateCommandBuffers(s.device, &allocate, &command), "Allocate command buffer");
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    checked(vkCreateSemaphore(s.device, &semaphore, nullptr, &s.acquired), "Create acquire semaphore");
    checked(vkCreateSemaphore(s.device, &semaphore, nullptr, &s.rendered), "Create render semaphore");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    checked(vkCreateFence(s.device, &fence, nullptr, &s.fence), "Create render fence");
    for (unsigned frame = 0; frame < 60; ++frame) {
        uint32_t index = 0;
        VkResult acquire = vkAcquireNextImageKHR(s.device, s.swapchain, 2000000000ull, s.acquired, VK_NULL_HANDLE, &index);
        if (acquire != VK_SUBOPTIMAL_KHR) checked(acquire, "Acquire image");
        checked(vkResetCommandBuffer(command, 0), "Reset command buffer");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(vkBeginCommandBuffer(command, &begin), "Begin command buffer");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[index]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkClearColorValue color{{0.04f, 0.12f + (frame / 20) * 0.16f, 0.22f + (frame / 20) * 0.12f, 1.f}};
        vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        checked(vkEndCommandBuffer(command), "End command buffer");
        checked(vkResetFences(s.device, 1, &s.fence), "Reset fence");
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &s.acquired; submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &s.rendered;
        checked(vkQueueSubmit(queue, 1, &submit, s.fence), "Submit clear");
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &s.rendered;
        present.swapchainCount = 1; present.pSwapchains = &s.swapchain; present.pImageIndices = &index;
        VkResult presented = vkQueuePresentKHR(queue, &present);
        if (presented != VK_SUBOPTIMAL_KHR) checked(presented, "Present image");
        checked(vkWaitForFences(s.device, 1, &s.fence, VK_TRUE, 2000000000ull), "Wait for clear");
        // Deliberate serialization for this short diagnostic, not a game render loop.
        checked(vkQueueWaitIdle(queue), "Wait for presentation");
    }
    std::ostringstream result;
    result << "PASS: 60 native Vulkan frames presented at " << extent.width << " x " << extent.height << ". Diagnostic only.";
    return result.str();
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gregorioruiz98_fefw_NativeBridge_renderTest(JNIEnv* env, jclass, jobject surface) {
    try { return env->NewStringUTF(render(env, surface).c_str()); }
    catch (const std::exception& error) { return env->NewStringUTF((std::string("FAILED: ") + error.what()).c_str()); }
}
