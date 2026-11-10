// SPDX-License-Identifier: GPL-3.0-or-later
// Independent Android platform probes, with no translated game code.
#include <jni.h>
#include <vulkan/vulkan.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace {
bool alias_test(long page) {
    const int fd = static_cast<int>(syscall(SYS_memfd_create, "fefw-probe", 1));
    if (fd < 0) return false;
    if (ftruncate(fd, page) != 0) { close(fd); return false; }
    void* first = mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    void* second = mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    bool ok = first != MAP_FAILED && second != MAP_FAILED;
    if (ok) {
        *static_cast<volatile uint32_t*>(first) = 0x46574645;
        ok = *static_cast<volatile uint32_t*>(second) == 0x46574645;
        ok = ok && mprotect(first, page, PROT_READ) == 0;
    }
    if (first != MAP_FAILED) munmap(first, page);
    if (second != MAP_FAILED) munmap(second, page);
    close(fd);
    return ok;
}

bool fixed_address_test(long page) {
    // A hint is non-destructive: existing mappings are never overwritten.
    // One page tests address availability, not the full game's memory layout.
    void* desired = reinterpret_cast<void*>(uintptr_t{0x7100000000ull});
    void* result = mmap(desired, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) return false;
    const bool ok = result == desired;
    munmap(result, page);
    return ok;
}

std::string quoted(const char* text) {
    std::string out = "\"";
    for (const unsigned char ch : std::string(text)) {
        if (ch == '"' || ch == '\\') out += '\\';
        if (ch >= 32) out += static_cast<char>(ch);
    }
    return out + "\"";
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_gregorioruiz98_fefw_NativeBridge_probe(JNIEnv* env, jclass) {
    const long page = sysconf(_SC_PAGESIZE);
    std::ostringstream report;
#if defined(__aarch64__)
    report << "{\"abi\":\"arm64-v8a\"";
#elif defined(__x86_64__)
    report << "{\"abi\":\"x86_64-test\"";
#else
#error Unsupported Android ABI
#endif
    report << ",\"page_size\":" << page
           << ",\"shared_memory_alias\":" << (page > 0 && alias_test(page) ? "true" : "false")
           << ",\"main_address_page\":" << (page > 0 && fixed_address_test(page) ? "true" : "false")
           << ",\"game_runtime_available\":false";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "FEFW Android diagnostics";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance{};
    VkResult result = vkCreateInstance(&create, nullptr, &instance);
    uint32_t count = 0;
    if (result == VK_SUCCESS) result = vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (result == VK_SUCCESS && count > 0) {
        std::vector<VkPhysicalDevice> devices(count);
        result = vkEnumeratePhysicalDevices(instance, &count, devices.data());
        if (result == VK_SUCCESS) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(devices[0], &properties);
            VkPhysicalDeviceFeatures features{};
            vkGetPhysicalDeviceFeatures(devices[0], &features);
            uint32_t extension_count = 0;
            bool external_host = false;
            if (vkEnumerateDeviceExtensionProperties(devices[0], nullptr, &extension_count, nullptr) == VK_SUCCESS) {
                std::vector<VkExtensionProperties> extensions(extension_count);
                if (vkEnumerateDeviceExtensionProperties(devices[0], nullptr, &extension_count, extensions.data()) == VK_SUCCESS)
                    for (const auto& extension : extensions)
                        if (std::strcmp(extension.extensionName, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME) == 0) external_host = true;
            }
            VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
            if (properties.apiVersion >= VK_API_VERSION_1_2) {
                VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
                query.pNext = &features12;
                vkGetPhysicalDeviceFeatures2(devices[0], &query);
            }
            char version[40];
            std::snprintf(version, sizeof version, "%u.%u.%u", VK_VERSION_MAJOR(properties.apiVersion),
                          VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
            report << ",\"gpu\":" << quoted(properties.deviceName)
                   << ",\"vulkan\":" << quoted(version)
                   << ",\"vulkan_1_3\":" << (properties.apiVersion >= VK_API_VERSION_1_3 ? "true" : "false")
                   << ",\"shader_int64\":" << (features.shaderInt64 ? "true" : "false")
                   << ",\"buffer_device_address\":" << (features12.bufferDeviceAddress ? "true" : "false")
                   << ",\"external_memory_host\":" << (external_host ? "true" : "false");
        }
    }
    report << ",\"vulkan_result\":" << result << ",\"gpu_count\":" << count << "}";
    if (instance) vkDestroyInstance(instance, nullptr);
    return env->NewStringUTF(report.str().c_str());
}
