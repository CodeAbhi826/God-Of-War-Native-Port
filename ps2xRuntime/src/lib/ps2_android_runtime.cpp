// Android platform hooks.
//
// The touch overlay is still a TODO. The Vulkan probe below runs once at
// startup so the first CI APK proves that the NDK, loader and the device's
// Vulkan 1.1 driver are all reachable before the GS backend is switched over.

#if defined(__ANDROID__)

#include <android/log.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    constexpr const char *kLogTag = "ps2x-vk";

    void logInfo(const char *message)
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
    }
}

void ps2AndroidVkProbe()
{
    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "ps2x-vk-probe";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.pEngineName = "ps2x";
    applicationInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult instanceResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (instanceResult != VK_SUCCESS)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "vkCreateInstance failed: %d", static_cast<int>(instanceResult));
        return;
    }

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "physical devices: %u", deviceCount);
    if (deviceCount == 0)
    {
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (uint32_t index = 0; index < deviceCount; ++index)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(devices[index], &properties);

        const uint32_t apiMajor = VK_VERSION_MAJOR(properties.apiVersion);
        const uint32_t apiMinor = VK_VERSION_MINOR(properties.apiVersion);
        const uint32_t apiPatch = VK_VERSION_PATCH(properties.apiVersion);
        const uint32_t driverMajor = VK_VERSION_MAJOR(properties.driverVersion);
        const uint32_t driverMinor = VK_VERSION_MINOR(properties.driverVersion);
        const uint32_t driverPatch = VK_VERSION_PATCH(properties.driverVersion);

        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "device[%u] '%s' api=%u.%u.%u driver=%u.%u.%u vendor=0x%04x id=0x%04x type=%d",
                            index, properties.deviceName, apiMajor, apiMinor, apiPatch,
                            driverMajor, driverMinor, driverPatch,
                            properties.vendorID, properties.deviceID,
                            static_cast<int>(properties.deviceType));

        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "limits: maxImage2D=%u maxStorageBufferRange=%u maxPushConstants=%u maxDescriptorSets=%u "
                            "maxComputeWorkGroupInvocations=%u",
                            properties.limits.maxImageDimension2D,
                            properties.limits.maxStorageBufferRange,
                            properties.limits.maxPushConstantsSize,
                            properties.limits.maxBoundDescriptorSets,
                            properties.limits.maxComputeWorkGroupInvocations);

        VkPhysicalDeviceFeatures deviceFeatures{};
        vkGetPhysicalDeviceFeatures(devices[index], &deviceFeatures);
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "features: samplerAnisotropy=%u textureCompressionASTC_LDR=%u fragmentStoresAndAtomics=%u "
                            "vertexPipelineStoresAndAtomics=%u shaderStorageImageWriteWithoutFormat=%u",
                            deviceFeatures.samplerAnisotropy,
                            deviceFeatures.textureCompressionASTC_LDR,
                            deviceFeatures.fragmentStoresAndAtomics,
                            deviceFeatures.vertexPipelineStoresAndAtomics,
                            deviceFeatures.shaderStorageImageWriteWithoutFormat);

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(devices[index], nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(devices[index], nullptr, &extensionCount, extensions.data());
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "device extensions: %u", extensionCount);
        for (const VkExtensionProperties &extension : extensions)
        {
            const bool interesting =
                std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, VK_KHR_MAINTENANCE1_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, VK_KHR_MAINTENANCE2_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, VK_KHR_MAINTENANCE3_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME) == 0 ||
                std::strcmp(extension.extensionName, "VK_EXT_depth_clamp") == 0 ||
                std::strcmp(extension.extensionName, "VK_EXT_rasterization_order_attachment_access") == 0;
            if (interesting)
            {
                __android_log_print(ANDROID_LOG_INFO, kLogTag, "  %s (rev %u)", extension.extensionName, extension.specVersion);
            }
        }
    }

    vkDestroyInstance(instance, nullptr);
}

#else

void ps2AndroidVkProbe() {}

#endif
