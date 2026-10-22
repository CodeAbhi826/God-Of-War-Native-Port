// Android platform hooks.
//
// The touch overlay is still a TODO. The Vulkan probe below runs once at
// startup so the first CI APK proves that the NDK, loader and the device's
// Vulkan 1.1 driver are all reachable before the GS backend is switched over.

#if defined(__ANDROID__)

#include <android/log.h>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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

    uint32_t instanceExtensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, instanceExtensions.data());
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "instance extensions: %u", instanceExtensionCount);
    for (const VkExtensionProperties &extension : instanceExtensions)
    {
        const bool interesting =
            std::strstr(extension.extensionName, "surface") != nullptr ||
            std::strstr(extension.extensionName, "headless") != nullptr;
        if (interesting)
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "  %s (rev %u)", extension.extensionName, extension.specVersion);
    }

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

void ps2AndroidSdlGpuProbe()
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    SDL_SetHint(SDL_HINT_APP_NAME, "PS2X");
    SDL_SetHint(SDL_HINT_APP_ID, "com.ps2x.runner");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_InitSubSystem(video) failed: %s", SDL_GetError());
        return;
    }
    const char *videoDriver = SDL_GetCurrentVideoDriver();
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL video driver: %s", videoDriver ? videoDriver : "(null)");

    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    if (!device)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_CreateGPUDevice(vulkan) failed: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL GPU driver: %s", SDL_GetGPUDeviceDriver(device));
    SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device);
    if (props)
    {
        const char *name = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "(unknown)");
        const char *driver = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "(unknown)");
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL GPU device: %s (%s)", name, driver);
    }
    SDL_Window *window = SDL_CreateWindow("ps2x-sdl-probe", 640, 448, 0);
    if (!window)
    {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_CreateWindow failed: %s", SDL_GetError());
    }
    else
    {
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL window created");
        if (!SDL_ClaimWindowForGPUDevice(device, window))
            __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        else
        {
            __android_log_print(ANDROID_LOG_INFO, kLogTag, "SDL window claimed by GPU device");
            SDL_ReleaseWindowFromGPUDevice(device, window);
        }
        SDL_DestroyWindow(window);
    }
    SDL_DestroyGPUDevice(device);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

#else

void ps2AndroidVkProbe() {}
void ps2AndroidSdlGpuProbe() {}

#endif
