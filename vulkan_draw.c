#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#define ANDROID_API_LEVEL 31
#include <vulkan/vulkan.h>
#include <vulkan/vk_android_native_buffer.h>
#include <android/hardware_buffer.h>
#include "hwvulkan.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CHECK(call) do { VkResult result = (call); if (result) { \
    fprintf(stderr, "%s failed: %d\n", #call, result); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
    if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

static uint32_t memory_type(VkPhysicalDeviceMemoryProperties *memory, uint32_t bits,
                             VkMemoryPropertyFlags flags)
{
    for (uint32_t i = 0; i < memory->memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (memory->memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    fprintf(stderr, "No compatible memory type: mask=%#x flags=%#x\n", bits, flags);
    exit(1);
}

static uint32_t *read_shader(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    if (length < 20 || length > 1048576 || length % 4) exit(1);
    rewind(f);
    uint32_t *data = malloc(length);
    if (!data || fread(data, 1, length, f) != (size_t)length) exit(1);
    fclose(f);
    *size = length;
    return data;
}

int main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    if (argc < 4 || argc > 5) return 2;
    int textured = argc == 5 && !strncmp(argv[4], "texture", 7);
    int vertex_store = argc == 5 && !strcmp(argv[4], "vertex_store");
    int native_image = argc == 5 && strstr(argv[4], "native");
    int ahb_image = argc == 5 && strstr(argv[4], "ahb");
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr get = dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!get) get = dlsym(library, "vkGetInstanceProcAddr");
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
        .pApplicationName = "g2_offscreen_bringup", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application};
    VkInstance instance;
    CHECK(create(&instance_info, NULL, &instance));
    LOAD(vkEnumeratePhysicalDevices);
    LOAD(vkGetPhysicalDeviceProperties);
    LOAD(vkGetPhysicalDeviceProperties2);
    LOAD(vkGetPhysicalDeviceMemoryProperties);
    LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD(vkCreateDevice);
    LOAD(vkGetDeviceQueue);
    LOAD(vkCreateImage);
    LOAD(vkGetImageMemoryRequirements);
    LOAD(vkAllocateMemory);
    LOAD(vkBindImageMemory);
    LOAD(vkCreateBuffer);
    LOAD(vkGetBufferMemoryRequirements);
    LOAD(vkBindBufferMemory);
    LOAD(vkMapMemory);
    LOAD(vkUnmapMemory);
    LOAD(vkCreateImageView);
    LOAD(vkCreateRenderPass);
    LOAD(vkCreateFramebuffer);
    LOAD(vkCreateShaderModule);
    LOAD(vkCreatePipelineLayout);
    LOAD(vkCreateSampler);
    LOAD(vkCreateDescriptorSetLayout);
    LOAD(vkCreateDescriptorPool);
    LOAD(vkAllocateDescriptorSets);
    LOAD(vkUpdateDescriptorSets);
    LOAD(vkCreateGraphicsPipelines);
    LOAD(vkCreateCommandPool);
    LOAD(vkAllocateCommandBuffers);
    LOAD(vkBeginCommandBuffer);
    LOAD(vkCmdBeginRenderPass);
    LOAD(vkCmdBindPipeline);
    LOAD(vkCmdBindDescriptorSets);
    LOAD(vkCmdCopyBufferToImage);
    LOAD(vkCmdDraw);
    LOAD(vkCmdEndRenderPass);
    LOAD(vkCmdPipelineBarrier);
    LOAD(vkCmdCopyImageToBuffer);
    LOAD(vkEndCommandBuffer);
    LOAD(vkCreateFence);
    LOAD(vkQueueSubmit);
    LOAD(vkWaitForFences);
    LOAD(vkResetFences);
    LOAD(vkDeviceWaitIdle);
    LOAD(vkDestroyDevice);
    LOAD(vkDestroyInstance);
    LOAD(vkDestroyFence);
    LOAD(vkDestroyCommandPool);
    LOAD(vkDestroyPipeline);
    LOAD(vkDestroyPipelineLayout);
    LOAD(vkDestroySampler);
    LOAD(vkDestroyDescriptorSetLayout);
    LOAD(vkDestroyDescriptorPool);
    LOAD(vkDestroyShaderModule);
    LOAD(vkDestroyFramebuffer);
    LOAD(vkDestroyRenderPass);
    LOAD(vkDestroyImageView);
    LOAD(vkDestroyBuffer);
    LOAD(vkDestroyImage);
    LOAD(vkFreeMemory);
    uint32_t count = 1;
    VkPhysicalDevice physical;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    if (!count) return 1;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    VkPhysicalDeviceSubgroupProperties subgroup = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceProperties2 props2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &subgroup};
    vkGetPhysicalDeviceProperties2(physical, &props2);
    printf("DEVICE %s subgroup=%u\n", props.deviceName, subgroup.subgroupSize);
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, NULL);
    VkQueueFamilyProperties *families = calloc(family_count, sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families);
    uint32_t family;
    for (family = 0; family < family_count; family++)
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    free(families);
    if (family == family_count) return 1;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info};
    const char *android_extensions[] = {"VK_ANDROID_native_buffer",
        "VK_ANDROID_external_memory_android_hardware_buffer"};
    if (native_image || ahb_image) {
        device_info.enabledExtensionCount = 2;
        device_info.ppEnabledExtensionNames = android_extensions;
    }
    VkPhysicalDeviceFeatures enabled = {.vertexPipelineStoresAndAtomics = vertex_store};
    device_info.pEnabledFeatures = &enabled;
    VkDevice device;
    CHECK(vkCreateDevice(physical, &device_info, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    AHardwareBuffer *android_buffer = NULL;
    void (*release_ahb)(AHardwareBuffer *) = NULL;
    VkNativeBufferANDROID native_info = {.sType = VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID};
    VkExternalMemoryImageCreateInfo external_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    if (native_image || ahb_image) {
        void *nw = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
        if (!nw) { fprintf(stderr, "%s\n", dlerror()); return 1; }
        int (*allocate_ahb)(const AHardwareBuffer_Desc *, AHardwareBuffer **) =
            dlsym(nw, "AHardwareBuffer_allocate");
        void (*describe_ahb)(const AHardwareBuffer *, AHardwareBuffer_Desc *) =
            dlsym(nw, "AHardwareBuffer_describe");
        const native_handle_t *(*native_handle)(const AHardwareBuffer *) =
            dlsym(nw, "AHardwareBuffer_getNativeHandle");
        release_ahb = dlsym(nw, "AHardwareBuffer_release");
        if (!allocate_ahb || !describe_ahb || !native_handle || !release_ahb) return 1;
        AHardwareBuffer_Desc desc = {.width = 16, .height = 16, .layers = 1,
            .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
            .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                     AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                     AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY | (1ull << 29)};
        if (allocate_ahb(&desc, &android_buffer)) return 1;
        describe_ahb(android_buffer, &desc);
        native_info.handle = native_handle(android_buffer);
        native_info.stride = desc.stride;
        native_info.format = desc.format;
        native_info.usage = desc.usage;
        native_info.usage3 = desc.usage;
        native_info.ahb = android_buffer;
        printf("ANDROID_IMAGE %s stride=%u\n", native_image ? "native" : "ahb", desc.stride);
    }
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = native_image ? (void *)&native_info : ahb_image ? (void *)&external_info : NULL,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {16, 16, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
    VkImage image;
    CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements image_req = {0};
    if (ahb_image) {
        PFN_vkGetAndroidHardwareBufferPropertiesANDROID get_ahb =
            (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)get(
                instance, "vkGetAndroidHardwareBufferPropertiesANDROID");
        if (!get_ahb) return 1;
        VkAndroidHardwareBufferPropertiesANDROID properties = {
            .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID};
        CHECK(get_ahb(device, android_buffer, &properties));
        image_req.size = properties.allocationSize;
        image_req.memoryTypeBits = properties.memoryTypeBits;
    } else {
        vkGetImageMemoryRequirements(device, image, &image_req);
    }
    VkMemoryDedicatedAllocateInfo dedicated = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = image};
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicated, .allocationSize = image_req.size,
        .memoryTypeIndex = memory_type(&memory, image_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkImportAndroidHardwareBufferInfoANDROID import_info = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
        .pNext = &dedicated, .buffer = android_buffer};
    if (ahb_image)
        allocation.pNext = &import_info;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    if (!native_image) {
        CHECK(vkAllocateMemory(device, &allocation, NULL, &image_memory));
        CHECK(vkBindImageMemory(device, image, image_memory, 0));
    }
    VkBufferCreateInfo buffer_info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 4096,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    VkBuffer buffer;
    CHECK(vkCreateBuffer(device, &buffer_info, NULL, &buffer));
    VkMemoryRequirements buffer_req;
    vkGetBufferMemoryRequirements(device, buffer, &buffer_req);
    allocation.pNext = NULL;
    allocation.allocationSize = buffer_req.size;
    allocation.memoryTypeIndex = memory_type(&memory, buffer_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory buffer_memory;
    CHECK(vkAllocateMemory(device, &allocation, NULL, &buffer_memory));
    CHECK(vkBindBufferMemory(device, buffer, buffer_memory, 0));
    void *pixels;
    CHECK(vkMapMemory(device, buffer_memory, 0, VK_WHOLE_SIZE, 0, &pixels));
    memset(pixels, 0, 4096);
    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image_info.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView view;
    CHECK(vkCreateImageView(device, &view_info, NULL, &view));
    VkImage texture_image = VK_NULL_HANDLE;
    VkDeviceMemory texture_memory = VK_NULL_HANDLE;
    VkImageView texture_view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    const uint8_t texels[16] = {255,0,0,255, 0,255,0,255,
                               0,0,255,255, 255,255,255,255};
    if (textured || vertex_store) {
      if (textured) {
        VkImageCreateInfo ti = image_info;
        ti.pNext = NULL;
        ti.extent.width = ti.extent.height = 2;
        ti.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        CHECK(vkCreateImage(device, &ti, NULL, &texture_image));
        VkMemoryRequirements tr;
        vkGetImageMemoryRequirements(device, texture_image, &tr);
        dedicated.image = texture_image;
        allocation.pNext = &dedicated;
        allocation.allocationSize = tr.size;
        allocation.memoryTypeIndex = memory_type(&memory, tr.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        CHECK(vkAllocateMemory(device, &allocation, NULL, &texture_memory));
        CHECK(vkBindImageMemory(device, texture_image, texture_memory, 0));
        VkImageViewCreateInfo tv = view_info;
        tv.image = texture_image;
        CHECK(vkCreateImageView(device, &tv, NULL, &texture_view));
        VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 1};
        CHECK(vkCreateSampler(device, &si, NULL, &sampler));
      }
        VkDescriptorSetLayoutBinding binding = {.binding = 0,
            .descriptorType = vertex_store ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1, .stageFlags = vertex_store ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT};
        VkDescriptorSetLayoutCreateInfo sli = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1, .pBindings = &binding};
        CHECK(vkCreateDescriptorSetLayout(device, &sli, NULL, &set_layout));
        VkDescriptorPoolSize ps = {binding.descriptorType, 1};
        VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps};
        CHECK(vkCreateDescriptorPool(device, &dpi, NULL, &descriptor_pool));
        VkDescriptorSetAllocateInfo dai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &set_layout};
        CHECK(vkAllocateDescriptorSets(device, &dai, &descriptor_set));
        VkDescriptorImageInfo di = {.sampler = sampler, .imageView = texture_view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo db = {.buffer = buffer, .offset = 2048, .range = 16};
        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set, .descriptorCount = 1,
            .descriptorType = binding.descriptorType,
            .pImageInfo = textured ? &di : NULL, .pBufferInfo = vertex_store ? &db : NULL};
        vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
        if (textured) memcpy(pixels, texels, sizeof(texels));
    }
    VkAttachmentDescription attachment = {.format = image_info.format, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &color};
    VkRenderPassCreateInfo render_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass};
    VkRenderPass render;
    CHECK(vkCreateRenderPass(device, &render_info, NULL, &render));
    VkFramebufferCreateInfo framebuffer_info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render, .attachmentCount = 1, .pAttachments = &view,
        .width = 16, .height = 16, .layers = 1};
    VkFramebuffer framebuffer;
    CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));
    VkShaderModule shaders[2];
    for (unsigned i = 0; i < 2; i++) {
        size_t size;
        uint32_t *spirv = read_shader(argv[2 + i], &size);
        VkShaderModuleCreateInfo shader_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = size, .pCode = spirv};
        CHECK(vkCreateShaderModule(device, &shader_info, NULL, &shaders[i]));
        free(spirv);
    }
    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (textured || vertex_store) {
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &set_layout;
    }
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(device, &layout_info, NULL, &layout));
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = shaders[0], .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = shaders[1], .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vertex = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {0, 0, 16, 16, 0, 1};
    VkRect2D scissor = {{0, 0}, {16, 16}};
    VkPipelineViewportStateCreateInfo viewport_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend_attachment = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend_attachment};
    VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport_info,
        .pRasterizationState = &raster, .pMultisampleState = &samples,
        .pColorBlendState = &blend, .layout = layout, .renderPass = render};
    VkPipeline pipeline;
    CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline));
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family};
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &pool_info, NULL, &pool));
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VkCommandBuffer command;
    CHECK(vkAllocateCommandBuffers(device, &command_info, &command));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin));
    if (textured) {
        VkImageMemoryBarrier tb = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture_image, .subresourceRange = view_info.subresourceRange};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, NULL, 0, NULL, 1, &tb);
        VkBufferImageCopy upload = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent = {2, 2, 1}};
        vkCmdCopyBufferToImage(command, buffer, texture_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload);
        tb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        tb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        tb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        tb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, NULL, 0, NULL, 1, &tb);
    }
    VkClearValue clear = {.color = {{0, 0, 0, 1}}};
    VkRenderPassBeginInfo render_begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render, .framebuffer = framebuffer, .renderArea = scissor,
        .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (textured || vertex_store)
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                 0, 1, &descriptor_set, 0, NULL);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = view_info.subresourceRange};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {16, 16, 1}};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    if (vertex_store) {
        VkMemoryBarrier memory_barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory_barrier, 0, NULL, 0, NULL);
    }
    CHECK(vkEndCommandBuffer(command));
    VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(vkCreateFence(device, &fence_info, NULL, &fence));
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1,
        .pCommandBuffers = &command};
    unsigned correct = 0;
    const uint8_t *bytes = pixels;
    unsigned frames = getenv("MALI_G2_TEST_FRAMES") ?
        strtoul(getenv("MALI_G2_TEST_FRAMES"), NULL, 0) : 1;
    if (!frames || frames > 120) return 2;
    unsigned tested_frames = 0;
    for (unsigned frame = 0; frame < frames; frame++) {
        if (textured) {
            uint8_t upload[16];
            for (unsigned i = 0; i < sizeof(upload); i++)
                upload[i] = frame % 2 && i % 4 != 3 ? 255 - texels[i] : texels[i];
            memcpy(pixels, upload, sizeof(upload));
        }
        if (frame) CHECK(vkResetFences(device, 1, &fence));
        CHECK(vkQueueSubmit(queue, 1, &submit, fence));
        CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(5000000000)));
        correct = 0;
        for (unsigned i = 0; i < 256; i++) {
            uint8_t expected[4] = {255,0,255,255};
            if (textured) {
                const uint8_t *source = texels + 4 * ((i % 16 >= 8) + 2 * (i / 16 >= 8));
                for (unsigned c = 0; c < 4; c++)
                    expected[c] = frame % 2 && c != 3 ? 255 - source[c] : source[c];
            }
            if (!memcmp(bytes + 4 * i, expected, 4)) correct++;
        }
        tested_frames++;
        if (correct != 256) break;
    }
    printf("VULKAN_DRAW_%s frames=%u/%u correct_pixels=%u/256 center=%02x%02x%02x%02x\n",
           correct == 256 ? "PASS" : "FAIL", tested_frames, frames, correct,
           bytes[4 * 136], bytes[4 * 136 + 1], bytes[4 * 136 + 2], bytes[4 * 136 + 3]);
    if (vertex_store) {
        const uint32_t *values = (const void *)(bytes + 2048);
        int passed = values[0] == 7 && values[1] == 7 && values[2] == 8 && values[3] == 11;
        printf("VERTEX_STORE_ATOMIC_%s mask=%#x values=%u,%u,%u\n",
               passed ? "PASS" : "FAIL", values[0], values[1], values[2], values[3]);
        if (!passed) correct = 0;
    }
    vkDeviceWaitIdle(device);
    vkUnmapMemory(device, buffer_memory);
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyPipelineLayout(device, layout, NULL);
    if (textured || vertex_store) {
        vkDestroyDescriptorPool(device, descriptor_pool, NULL);
        vkDestroyDescriptorSetLayout(device, set_layout, NULL);
    }
    if (textured) {
        vkDestroySampler(device, sampler, NULL);
        vkDestroyImageView(device, texture_view, NULL);
        vkDestroyImage(device, texture_image, NULL);
        vkFreeMemory(device, texture_memory, NULL);
    }
    vkDestroyShaderModule(device, shaders[0], NULL);
    vkDestroyShaderModule(device, shaders[1], NULL);
    vkDestroyFramebuffer(device, framebuffer, NULL);
    vkDestroyRenderPass(device, render, NULL);
    vkDestroyImageView(device, view, NULL);
    vkDestroyBuffer(device, buffer, NULL);
    vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, buffer_memory, NULL);
    vkFreeMemory(device, image_memory, NULL);
    if (android_buffer)
        release_ahb(android_buffer);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    if (hal) hal->common.close(&hal->common);
    dlclose(library);
    return correct == 256 ? 0 : 1;
}
