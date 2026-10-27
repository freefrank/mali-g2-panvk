#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hwvulkan.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CHECK(call) do { VkResult result = (call); if (result) { \
    fprintf(stderr, "%s failed: %d\n", #call, result); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
    if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

static uint32_t memory_type(VkPhysicalDeviceMemoryProperties *properties, uint32_t bits,
                            VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < properties->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (properties->memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    fprintf(stderr, "No memory type bits=%#x flags=%#x\n", bits, flags);
    exit(1);
}

static int run_astc(PFN_vkGetInstanceProcAddr get, VkInstance instance,
                    VkPhysicalDevice physical, VkDevice device, VkPipeline pipeline,
                    VkPipelineLayout layout, VkDescriptorSetLayout set_layout)
{
    LOAD(vkGetPhysicalDeviceMemoryProperties);
    LOAD(vkGetDeviceQueue);
    LOAD(vkCreateBuffer);
    LOAD(vkGetBufferMemoryRequirements);
    LOAD(vkAllocateMemory);
    LOAD(vkBindBufferMemory);
    LOAD(vkMapMemory);
    LOAD(vkUnmapMemory);
    LOAD(vkCreateImage);
    LOAD(vkGetImageMemoryRequirements);
    LOAD(vkBindImageMemory);
    LOAD(vkCreateImageView);
    LOAD(vkCreateDescriptorPool);
    LOAD(vkAllocateDescriptorSets);
    LOAD(vkUpdateDescriptorSets);
    LOAD(vkCreateCommandPool);
    LOAD(vkAllocateCommandBuffers);
    LOAD(vkBeginCommandBuffer);
    LOAD(vkCmdPipelineBarrier);
    LOAD(vkCmdBindPipeline);
    LOAD(vkCmdBindDescriptorSets);
    LOAD(vkCmdPushConstants);
    LOAD(vkCmdDispatch);
    LOAD(vkCmdCopyImageToBuffer);
    LOAD(vkEndCommandBuffer);
    LOAD(vkCreateFence);
    LOAD(vkQueueSubmit);
    LOAD(vkWaitForFences);
    LOAD(vkDeviceWaitIdle);
    LOAD(vkDestroyFence);
    LOAD(vkDestroyCommandPool);
    LOAD(vkDestroyDescriptorPool);
    LOAD(vkDestroyImageView);
    LOAD(vkDestroyImage);
    LOAD(vkDestroyBuffer);
    LOAD(vkFreeMemory);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4096, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type(&properties,
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    VkDeviceMemory buffer_memory;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &buffer_memory));
    CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    uint8_t *mapped;
    CHECK(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
    memset(mapped, 0, 4096);
    uint8_t compressed[256];
    const char *input_path = getenv("MALI_G2_ASTC_INPUT");
    if (input_path) {
        FILE *f = fopen(input_path, "rb");
        if (!f || fread(compressed, 1, sizeof(compressed), f) != sizeof(compressed)) return 1;
        fclose(f);
    }
    for (unsigned by = 0; by < 4; by++) {
        for (unsigned bx = 0; bx < 4; bx++) {
            unsigned x = bx * 16;
            unsigned offset = ((x & 32) << 3) | ((by & 6) << 5) |
                              ((x & 16) << 1) | ((by & 1) << 4) | (x & 15);
            uint16_t color[4] = {bx * 85 * 257, by * 85 * 257, (255 - bx * 85) * 257, 65535};
            uint64_t header = UINT64_C(0xfffffffffffffdfc);
            if (input_path) {
                memcpy(mapped + offset, compressed + 16 * (by * 4 + bx), 16);
            } else {
                memcpy(mapped + offset, &header, 8);
                memcpy(mapped + offset + 8, color, 8);
            }
        }
    }
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {16, 16, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage image;
    CHECK(vkCreateImage(device, &image_info, NULL, &image));
    vkGetImageMemoryRequirements(device, image, &requirements);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memory_type(&properties, requirements.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory image_memory;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &image_memory));
    CHECK(vkBindImageMemory(device, image, image_memory, 0));
    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY, .format = image_info.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView view;
    CHECK(vkCreateImageView(device, &view_info, NULL, &view));
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
                                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes};
    VkDescriptorPool descriptor_pool;
    CHECK(vkCreateDescriptorPool(device, &pool_info, NULL, &descriptor_pool));
    VkDescriptorSetAllocateInfo set_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &set_layout};
    VkDescriptorSet set;
    CHECK(vkAllocateDescriptorSets(device, &set_info, &set));
    VkDescriptorBufferInfo descriptor_buffer = {.buffer = buffer, .range = 1024};
    VkDescriptorImageInfo descriptor_image = {.imageView = view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet writes[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
         .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .pBufferInfo = &descriptor_buffer},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
         .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .pImageInfo = &descriptor_image},
    };
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
    VkCommandPoolCreateInfo command_pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0};
    VkCommandPool command_pool;
    CHECK(vkCreateCommandPool(device, &command_pool_info, NULL, &command_pool));
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = image,
        .subresourceRange = view_info.subresourceRange};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    uint32_t push[7] = {4, 4, 512, 512, 9, 0, 0};
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
    vkCmdDispatch(command, 1, 1, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.bufferOffset = 1024,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {16, 16, 1}};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkQueue queue;
    vkGetDeviceQueue(device, 0, 0, &queue);
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &command};
    printf("ASTC_DISPATCH_BEGIN\n");
    CHECK(vkQueueSubmit(queue, 1, &submit, fence));
    CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
    const char *output_path = getenv("MALI_G2_ASTC_OUTPUT");
    if (output_path) {
        FILE *f = fopen(output_path, "wb");
        if (!f || fwrite(mapped + 1024, 1, 1024, f) != 1024) return 1;
        fclose(f);
    }
    uint8_t reference[1024];
    const char *reference_path = getenv("MALI_G2_ASTC_REFERENCE");
    if (reference_path) {
        FILE *f = fopen(reference_path, "rb");
        if (!f || fread(reference, 1, sizeof(reference), f) != sizeof(reference)) return 1;
        fclose(f);
    }
    unsigned correct = 0;
    for (unsigned y = 0; y < 16; y++) {
        for (unsigned x = 0; x < 16; x++) {
            uint8_t expected[4] = {x / 4 * 85, y / 4 * 85, 255 - x / 4 * 85, 255};
            if (reference_path)
                memcpy(expected, reference + 4 * (y * 16 + x), 4);
            if (!memcmp(mapped + 1024 + 4 * (y * 16 + x), expected, 4)) correct++;
        }
    }
    if (input_path && !reference_path)
        printf("ASTC_REFERENCE_CAPTURE pixels=256\n");
    else
        printf("ASTC_DISPATCH_%s correct_pixels=%u/256 first=%02x%02x%02x%02x\n",
               correct == 256 ? "PASS" : "FAIL", correct,
               mapped[1024], mapped[1025], mapped[1026], mapped[1027]);
    vkDeviceWaitIdle(device);
    vkUnmapMemory(device, buffer_memory);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    vkDestroyImageView(device, view, NULL);
    vkDestroyImage(device, image, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, image_memory, NULL);
    vkFreeMemory(device, buffer_memory, NULL);
    return (input_path && !reference_path) || correct == 256 ? 0 : 1;
}

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
        if (strstr(argv[s], "astc") && getenv("MALI_G2_RUN_ASTC")) {
            int result = run_astc(get, instance, physical, device, pipeline, layout, set);
            if (result) return result;
        }
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
