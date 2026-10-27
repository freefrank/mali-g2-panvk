#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#define ANDROID_API_LEVEL 31
#include <vulkan/vulkan.h>
#include <vulkan/vk_android_native_buffer.h>
#include <android/hardware_buffer.h>
#include "hwvulkan.h"
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s: %d\n", #call, r); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
    if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    if (argc != 2) return 2;
    void *nativewindow = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
    if (!nativewindow) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **) =
        dlsym(nativewindow, "AHardwareBuffer_allocate");
    void (*describe)(const AHardwareBuffer *, AHardwareBuffer_Desc *) =
        dlsym(nativewindow, "AHardwareBuffer_describe");
    void (*release)(AHardwareBuffer *) = dlsym(nativewindow, "AHardwareBuffer_release");
    const native_handle_t *(*handle)(const AHardwareBuffer *) =
        dlsym(nativewindow, "AHardwareBuffer_getNativeHandle");
    if (!allocate || !describe || !release || !handle) return 1;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr get = dlsym(library, "vk_icdGetInstanceProcAddr");
    PFN_vkCreateInstance create = NULL;
    hwvulkan_device_t *hal = NULL;
    if (!get) {
        hwvulkan_module_t *module = dlsym(library, HAL_MODULE_INFO_SYM_AS_STR);
        if (!module || module->common.methods->open(&module->common, HWVULKAN_DEVICE_0,
                                                   (hw_device_t **)&hal)) return 1;
        get = hal->GetInstanceProcAddr;
        create = hal->CreateInstance;
    }
    if (!create) create = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "g2_android_import", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(create(&ici, NULL, &instance));
    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkCreateDevice);
    LOAD(vkDestroyDevice);
    LOAD(vkDestroyInstance);
    LOAD(vkGetDeviceProcAddr);
    uint32_t count = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    if (!count) return 1;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    printf("DEVICE %s\n", properties.deviceName);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
    const char *extensions[] = {"VK_ANDROID_native_buffer",
        "VK_ANDROID_external_memory_android_hardware_buffer", "VK_KHR_external_memory_fd"};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 3, .ppEnabledExtensionNames = extensions};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb =
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(
            device, "vkGetAndroidHardwareBufferPropertiesANDROID");
    PFN_vkCreateImage create_image =
        (PFN_vkCreateImage)vkGetDeviceProcAddr(device, "vkCreateImage");
    PFN_vkDestroyImage destroy_image =
        (PFN_vkDestroyImage)vkGetDeviceProcAddr(device, "vkDestroyImage");
    PFN_vkAllocateMemory allocate_memory =
        (PFN_vkAllocateMemory)vkGetDeviceProcAddr(device, "vkAllocateMemory");
    PFN_vkFreeMemory free_memory =
        (PFN_vkFreeMemory)vkGetDeviceProcAddr(device, "vkFreeMemory");
    PFN_vkBindImageMemory bind_memory =
        (PFN_vkBindImageMemory)vkGetDeviceProcAddr(device, "vkBindImageMemory");
    if (!get_ahb || !create_image || !destroy_image || !allocate_memory ||
        !free_memory || !bind_memory) return 1;
    uint64_t usages[] = {
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT,
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
            AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY | (1ull << 29),
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
            AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
    };
    int failures = 0;
    for (unsigned i = 0; i < sizeof(usages) / sizeof(usages[0]); i++) {
        AHardwareBuffer_Desc desc = {.width = 1280, .height = 720, .layers = 1,
            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, .usage = usages[i]};
        AHardwareBuffer *ahb = NULL;
        int ret = allocate(&desc, &ahb);
        printf("CASE %u allocate=%d usage=%#" PRIx64 "\n", i, ret, usages[i]);
        if (ret) { failures++; continue; }
        describe(ahb, &desc);
        const native_handle_t *native = handle(ahb);
        printf("HANDLE stride=%u fds=%d ints=%d\n", desc.stride, native->numFds, native->numInts);
        for (int j = 0; j < native->numFds; j++) {
            printf("FD %d size=%lld\n", native->data[j],
                   (long long)lseek(native->data[j], 0, SEEK_END));
            lseek(native->data[j], 0, SEEK_SET);
        }
        for (int j = native->numFds; j < native->numFds + native->numInts; j++)
            printf("H[%d]=%#x\n", j, native->data[j]);
        VkAndroidHardwareBufferFormatPropertiesANDROID fp = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
        VkAndroidHardwareBufferPropertiesANDROID props = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, .pNext = &fp};
        VkResult result = get_ahb(device, ahb, &props);
        printf("AHB_PROPERTIES result=%d size=%" PRIu64 " bits=%#x format=%u\n",
               result, props.allocationSize, props.memoryTypeBits, fp.format);
        if (result) failures++;
        VkNativeBufferANDROID anb = {.sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID,
            .handle = native, .stride = desc.stride, .format = desc.format,
            .usage = (int)desc.usage};
        VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = &anb, .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .extent = {desc.width, desc.height, 1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
        VkImage image = VK_NULL_HANDLE;
        result = create_image(device, &image_info, NULL, &image);
        printf("NATIVE_IMAGE result=%d\n", result);
        if (result) failures++;
        else destroy_image(device, image, NULL);
        VkExternalMemoryImageCreateInfo external = {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
        image_info.pNext = &external;
        image = VK_NULL_HANDLE;
        result = create_image(device, &image_info, NULL, &image);
        printf("AHB_IMAGE result=%d\n", result);
        if (!result && props.memoryTypeBits) {
            VkMemoryDedicatedAllocateInfo dedicated = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = image};
            VkImportAndroidHardwareBufferInfoANDROID imported = {
                .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
                .pNext = &dedicated, .buffer = ahb};
            VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = &imported, .allocationSize = props.allocationSize,
                .memoryTypeIndex = __builtin_ctz(props.memoryTypeBits)};
            VkDeviceMemory memory;
            result = allocate_memory(device, &allocation, NULL, &memory);
            printf("AHB_MEMORY result=%d\n", result);
            if (!result) {
                result = bind_memory(device, image, memory, 0);
                printf("AHB_BIND result=%d\n", result);
                destroy_image(device, image, NULL);
                image = VK_NULL_HANDLE;
                free_memory(device, memory, NULL);
            }
        }
        if (result) failures++;
        if (image) destroy_image(device, image, NULL);
        release(ahb);
    }
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    printf("FAILURES %d\n", failures);
    return failures ? 1 : 0;
}
