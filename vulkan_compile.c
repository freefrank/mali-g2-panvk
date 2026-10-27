#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hwvulkan.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call) do { VkResult result = (call); if (result) { \
    fprintf(stderr, "%s failed: %d\n", #call, result); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
    if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    if (argc < 3) return 2;
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
    VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "g2_compute_compile", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application};
    VkInstance instance;
    CHECK(create(&instance_info, NULL, &instance));
    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkGetPhysicalDeviceFeatures);
    LOAD(vkCreateDevice);
    LOAD(vkCreateDescriptorSetLayout);
    LOAD(vkCreatePipelineLayout);
    LOAD(vkCreateShaderModule);
    LOAD(vkCreateComputePipelines);
    LOAD(vkDestroyPipeline);
    LOAD(vkDestroyShaderModule);
    LOAD(vkDestroyPipelineLayout);
    LOAD(vkDestroyDescriptorSetLayout);
    LOAD(vkDestroyDevice);
    LOAD(vkDestroyInstance);
    uint32_t count = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    if (!count) return 1;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    printf("DEVICE %s\n", properties.deviceName);
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(physical, &features);
    features.robustBufferAccess = getenv("MALI_G2_COMPILE_ROBUST") != NULL;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue, .pEnabledFeatures = &features};
    VkDevice device;
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    for (int s = 2; s < argc; s++) {
        FILE *f = fopen(argv[s], "rb");
        if (!f) { perror(argv[s]); return 1; }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        if (size < 20 || size % 4 || size > 4194304) return 1;
        rewind(f);
        uint32_t *code = malloc(size);
        if (!code || fread(code, 1, size, f) != (size_t)size) return 1;
        fclose(f);
        VkShaderModuleCreateInfo module_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = size, .pCode = code};
        VkShaderModule module;
        CHECK(vkCreateShaderModule(device, &module_info, NULL, &module));
        VkDescriptorSetLayoutBinding bindings[3];
        memset(bindings, 0, sizeof(bindings));
        int image = strstr(argv[s], "astc") != NULL || strstr(argv[s], "unswizzle") != NULL;
        for (unsigned i = 0; i < 3; i++) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = image && i == 1 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE :
                                                                         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 3, .pBindings = bindings};
        VkDescriptorSetLayout set;
        CHECK(vkCreateDescriptorSetLayout(device, &set_info, NULL, &set));
        VkPushConstantRange push = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = 128};
        VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &set, .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push};
        VkPipelineLayout layout;
        CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
        VkComputePipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"},
            .layout = layout};
        VkPipeline pipeline;
        printf("COMPILE_BEGIN %s bytes=%ld robust=%u\n", argv[s], size, features.robustBufferAccess);
        CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
        printf("COMPILE_PASS %s\n", argv[s]);
        vkDestroyPipeline(device, pipeline, NULL);
        vkDestroyPipelineLayout(device, layout, NULL);
        vkDestroyDescriptorSetLayout(device, set, NULL);
        vkDestroyShaderModule(device, module, NULL);
        free(code);
    }
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
