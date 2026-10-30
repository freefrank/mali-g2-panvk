#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #call, r); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
    if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

static uint32_t
memory_type(const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags flags)
{
    for (uint32_t i=0; i<mp->memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & flags) == flags) return i;
    exit(1);
}

static float
ufloat(uint32_t v, unsigned mantissa)
{
    unsigned e = v >> mantissa, m = v & ((1u << mantissa) - 1);
    if (!e) return (float)m / (1u << (14 + mantissa));
    uint32_t bits = (e == 31 ? 255 : e + 112) << 23 | m << (23 - mantissa);
    float f; memcpy(&f, &bits, 4); return f;
}

static float
channel(const void *data, size_t pixel, unsigned c, VkFormat format)
{
    if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
        _Float16 v; memcpy(&v, (const uint8_t *)data + pixel * 8 + c * 2, 2); return v;
    }
    if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
        uint32_t v = ((const uint32_t *)data)[pixel];
        return c == 3 ? 1.0f : c == 2 ? ufloat(v >> 22, 5) : ufloat(v >> (c * 11) & 2047, 6);
    }
    return ((const uint8_t *)data)[pixel * 4 + c] / 255.0f;
}

int
main(int argc, char **argv)
{
    setbuf(stdout, NULL);
    if (argc != 6) { fprintf(stderr, "usage: %s driver store.spv load.spv VkFormat samples\n", argv[0]); return 2; }
    const uint32_t w=65, h=33, samples=strtoul(argv[5], NULL, 10);
    VkFormat format=strtoul(argv[4], NULL, 10);
    const uint32_t layer=getenv("MALI_G2_MSAA_LAYER") ? 2 : 0;
    const int probe=getenv("MALI_G2_MSAA_PROBE") != NULL;
    const int clear_init=getenv("MALI_G2_MSAA_CLEAR") != NULL;
    if ((samples != 2 && samples != 4 && samples != 8 && samples != 16) ||
        (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R16G16B16A16_SFLOAT &&
         format != VK_FORMAT_B10G11R11_UFLOAT_PACK32)) return 2;
    void *library=dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr get=dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!get) get=dlsym(library, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance create=NULL;
    if (!get) {
        hwvulkan_module_t *module=dlsym(library, HAL_MODULE_INFO_SYM_AS_STR);
        hwvulkan_device_t *hal=NULL;
        if (!module || module->common.methods->open(&module->common, HWVULKAN_DEVICE_0, (hw_device_t **)&hal)) return 1;
        get=hal->GetInstanceProcAddr; create=hal->CreateInstance;
    }
    if (!create) create=(PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName="g2-msaa-storage", .apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
    VkInstance instance; CHECK(create(&ici, NULL, &instance));
    LOAD(vkEnumeratePhysicalDevices); LOAD(vkGetPhysicalDeviceFeatures); LOAD(vkGetPhysicalDeviceMemoryProperties);
    LOAD(vkGetPhysicalDeviceImageFormatProperties); LOAD(vkCreateDevice); LOAD(vkGetDeviceQueue);
    LOAD(vkCreateBuffer); LOAD(vkGetBufferMemoryRequirements); LOAD(vkAllocateMemory); LOAD(vkBindBufferMemory);
    LOAD(vkMapMemory); LOAD(vkUnmapMemory); LOAD(vkCreateImage); LOAD(vkGetImageMemoryRequirements);
    LOAD(vkBindImageMemory); LOAD(vkCreateImageView); LOAD(vkCreateShaderModule);
    LOAD(vkCreateDescriptorSetLayout); LOAD(vkCreatePipelineLayout); LOAD(vkCreateComputePipelines);
    LOAD(vkCreateDescriptorPool); LOAD(vkAllocateDescriptorSets); LOAD(vkUpdateDescriptorSets);
    LOAD(vkCreateCommandPool); LOAD(vkAllocateCommandBuffers); LOAD(vkBeginCommandBuffer); LOAD(vkEndCommandBuffer);
    LOAD(vkCmdPipelineBarrier); LOAD(vkCmdBindPipeline); LOAD(vkCmdBindDescriptorSets); LOAD(vkCmdPushConstants);
    LOAD(vkCmdDispatch); LOAD(vkCmdResolveImage); LOAD(vkCmdCopyImageToBuffer); LOAD(vkQueueSubmit); LOAD(vkQueueWaitIdle);
    LOAD(vkCmdClearColorImage); LOAD(vkCreateSampler); LOAD(vkDestroySampler);
    LOAD(vkDestroyCommandPool); LOAD(vkDestroyDescriptorPool); LOAD(vkDestroyPipeline); LOAD(vkDestroyPipelineLayout);
    LOAD(vkDestroyDescriptorSetLayout); LOAD(vkDestroyShaderModule); LOAD(vkDestroyImageView); LOAD(vkDestroyImage);
    LOAD(vkDestroyBuffer); LOAD(vkFreeMemory); LOAD(vkDestroyDevice); LOAD(vkDestroyInstance);
    uint32_t count=1; VkPhysicalDevice physical; CHECK(vkEnumeratePhysicalDevices(instance, &count, &physical));
    VkPhysicalDeviceFeatures supported, enabled={0}; vkGetPhysicalDeviceFeatures(physical, &supported);
    VkImageUsageFlags usage=VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (clear_init) usage=(usage & ~VK_IMAGE_USAGE_STORAGE_BIT) | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageFormatProperties props={0};
    VkResult result=vkGetPhysicalDeviceImageFormatProperties(physical, format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, usage, 0, &props);
    printf("MSAA_CAPS feature=%u sample_counts=%#x query=%d probe=%d\n",
        supported.shaderStorageImageMultisample, props.sampleCounts, result, probe);
    if (!probe && ((!clear_init && !supported.shaderStorageImageMultisample) || result || !(props.sampleCounts & samples))) return 2;
    enabled.shaderStorageImageMultisample=supported.shaderStorageImageMultisample;
    float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=0, .queueCount=1, .pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1,
        .pQueueCreateInfos=&qci, .pEnabledFeatures=&enabled};
    VkDevice device; CHECK(vkCreateDevice(physical, &dci, NULL, &device));
    VkQueue queue; vkGetDeviceQueue(device, 0, 0, &queue);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    VkBuffer buffers[2]; VkDeviceMemory buffer_memory[2]; void *mapped[2];
    VkDeviceSize sizes[2]={(VkDeviceSize)w*h*samples*16, (VkDeviceSize)w*h*(format == 97 ? 8 : 4)};
    for (unsigned i=0; i<2; i++) {
        VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=sizes[i],
            .usage=i ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
        CHECK(vkCreateBuffer(device, &bi, NULL, &buffers[i]));
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(device, buffers[i], &mr);
        VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=mr.size,
            .memoryTypeIndex=memory_type(&mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        CHECK(vkAllocateMemory(device, &ai, NULL, &buffer_memory[i]));
        CHECK(vkBindBufferMemory(device, buffers[i], buffer_memory[i], 0));
        CHECK(vkMapMemory(device, buffer_memory[i], 0, sizes[i], 0, &mapped[i]));
        memset(mapped[i], 0xa5, sizes[i]);
    }
    VkImage images[2]; VkDeviceMemory image_memory[2];
    for (unsigned i=0; i<2; i++) {
        VkImageCreateInfo ii={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType=VK_IMAGE_TYPE_2D,
            .format=format, .extent={w,h,1}, .mipLevels=1, .arrayLayers=layer+1,
            .samples=i ? VK_SAMPLE_COUNT_1_BIT : samples, .tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=i ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT : usage};
        CHECK(vkCreateImage(device, &ii, NULL, &images[i]));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(device, images[i], &mr);
        VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=mr.size,
            .memoryTypeIndex=memory_type(&mp, mr.memoryTypeBits, 0)};
        CHECK(vkAllocateMemory(device, &ai, NULL, &image_memory[i]));
        CHECK(vkBindImageMemory(device, images[i], image_memory[i], 0));
    }
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image=images[0],
        .viewType=layer ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
        .format=format, .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,layer+1}};
    VkImageView view; CHECK(vkCreateImageView(device, &vi, NULL, &view));
    VkSampler sampler=VK_NULL_HANDLE;
    if (clear_init) {
        VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter=VK_FILTER_NEAREST, .minFilter=VK_FILTER_NEAREST,
            .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
        CHECK(vkCreateSampler(device,&sci,NULL,&sampler));
    }
    VkDescriptorSetLayoutBinding bindings[2]={{.binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}, {.binding=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    if (clear_init) bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorSetLayoutCreateInfo sli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount=2, .pBindings=bindings};
    VkDescriptorSetLayout set_layout; CHECK(vkCreateDescriptorSetLayout(device, &sli, NULL, &set_layout));
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,20};
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount=1,
        .pSetLayouts=&set_layout, .pushConstantRangeCount=1, .pPushConstantRanges=&range};
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device, &pli, NULL, &layout));
    VkPipeline pipelines[2]={0}; VkShaderModule shaders[2]={0};
    for (unsigned i=clear_init ? 1 : 0; i<2; i++) {
        FILE *f=fopen(argv[2+i], "rb"); if (!f || fseek(f,0,SEEK_END)) return 1;
        long length=ftell(f); if (length<20 || length>1048576 || length%4) return 1;
        rewind(f); uint32_t *code=malloc(length); if (!code || fread(code,1,length,f)!=(size_t)length) return 1; fclose(f);
        VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize=length, .pCode=code};
        CHECK(vkCreateShaderModule(device, &smi, NULL, &shaders[i])); free(code);
        VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage=VK_SHADER_STAGE_COMPUTE_BIT,
                .module=shaders[i], .pName="main"}, .layout=layout};
        CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pi, NULL, &pipelines[i]));
    }
    VkDescriptorPoolSize pool_sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    pool_sizes[0].type=bindings[0].descriptorType;
    VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets=1, .poolSizeCount=2, .pPoolSizes=pool_sizes};
    VkDescriptorPool pool; CHECK(vkCreateDescriptorPool(device, &dpi, NULL, &pool));
    VkDescriptorSetAllocateInfo sai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool=pool, .descriptorSetCount=1, .pSetLayouts=&set_layout};
    VkDescriptorSet set; CHECK(vkAllocateDescriptorSets(device, &sai, &set));
    VkDescriptorImageInfo image_info={.sampler=sampler, .imageView=view, .imageLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo buffer_info={buffers[0],0,sizes[0]};
    VkWriteDescriptorSet writes[2]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=set, .dstBinding=0,
        .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo=&image_info},
        {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=set, .dstBinding=1, .descriptorCount=1,
         .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo=&buffer_info}};
    writes[0].descriptorType=bindings[0].descriptorType;
    vkUpdateDescriptorSets(device,2,writes,0,NULL);
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex=0};
    VkCommandPool command_pool; CHECK(vkCreateCommandPool(device,&cpi,NULL,&command_pool));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=command_pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1};
    VkCommandBuffer command; CHECK(vkAllocateCommandBuffers(device,&cai,&command));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command,&begin));
    VkImageMemoryBarrier barriers[2];
    for (unsigned i=0; i<2; i++) barriers[i]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask=i || clear_init ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED, .newLayout=VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=images[i], .subresourceRange=vi.subresourceRange};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,barriers);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    uint32_t push[5]={w,h,samples,layer,3};
    vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);
    if (clear_init) {
        VkClearColorValue clear={.float32={0.25f,0.5f,0.75f,1.0f}};
        if (layer) {
            VkClearColorValue zero={0};
            vkCmdClearColorImage(command,images[0],VK_IMAGE_LAYOUT_GENERAL,&zero,1,&vi.subresourceRange);
            VkImageMemoryBarrier ordering=barriers[0];
            ordering.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
            ordering.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            ordering.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,0,NULL,0,NULL,1,&ordering);
        }
        VkImageSubresourceRange selected=vi.subresourceRange;
        selected.baseArrayLayer=layer; selected.layerCount=1;
        vkCmdClearColorImage(command,images[0],VK_IMAGE_LAYOUT_GENERAL,&clear,1,&selected);
    } else {
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[0]);
        vkCmdDispatch(command,(w+7)/8,(h+7)/8,1);
    }
    barriers[0].oldLayout=VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcAccessMask=clear_init ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    barriers[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,clear_init ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,barriers);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[1]);
    vkCmdDispatch(command,(w+7)/8,(h+7)/8,1);
    VkImageResolve resolve={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,layer+1},
        .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,layer+1}, .extent={w,h,1}};
    vkCmdResolveImage(command,images[0],VK_IMAGE_LAYOUT_GENERAL,images[1],VK_IMAGE_LAYOUT_GENERAL,1,&resolve);
    barriers[1].oldLayout=VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barriers[1]);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,layer,1}, .imageExtent={w,h,1}};
    vkCmdCopyImageToBuffer(command,images[1],VK_IMAGE_LAYOUT_GENERAL,buffers[1],1,&copy);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(command));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount=1, .pCommandBuffers=&command};
    CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue));
    unsigned sample_bad=0, resolve_bad=0;
    for (size_t p=0; p<(size_t)w*h; p++) {
        float sum[4]={0};
        for (unsigned s=0; s<samples; s++) for (unsigned c=0; c<4; c++) {
            float wanted=c==0 ? ((p%w+3)&7)/8.0f : c==1 ? ((p/w+3)&7)/8.0f : c==2 ? (s&7)/8.0f : ((s+3)&3)/4.0f;
            if (clear_init) wanted=(c+1)/4.0f;
            if (format==VK_FORMAT_R8G8B8A8_UNORM) wanted=(unsigned)(wanted*255+0.5f)/255.0f;
            if (format==VK_FORMAT_B10G11R11_UFLOAT_PACK32 && c==3) wanted=1;
            float got=((float *)mapped[0])[(p*samples+s)*4+c];
            if (!(fabsf(got-wanted)<0.0006f)) {
                if (!sample_bad) printf("SAMPLE_BAD pixel=%zu sample=%u channel=%u got=%g wanted=%g\n",p,s,c,got,wanted);
                sample_bad++;
            }
            sum[c]+=wanted/samples;
        }
        for (unsigned c=0; c<4; c++) {
            float got=channel(mapped[1],p,c,format);
            float tolerance=format==VK_FORMAT_R8G8B8A8_UNORM ? 0.004f : 0.0006f;
            if (!(fabsf(got-sum[c])<tolerance)) {
                if (!resolve_bad) printf("RESOLVE_BAD pixel=%zu channel=%u got=%g wanted=%g\n",p,c,got,sum[c]);
                resolve_bad++;
            }
        }
    }
    printf("MSAA_%s format=%u samples=%u layer=%u sample_bad=%u resolve_bad=%u\n",
        sample_bad || resolve_bad ? "FAIL" : "PASS",format,samples,layer,sample_bad,resolve_bad);
    vkDestroyCommandPool(device,command_pool,NULL); vkDestroyDescriptorPool(device,pool,NULL);
    vkDestroyPipelineLayout(device,layout,NULL); vkDestroyDescriptorSetLayout(device,set_layout,NULL);
    vkDestroyImageView(device,view,NULL);
    vkDestroySampler(device,sampler,NULL);
    for (unsigned i=0; i<2; i++) {
        vkDestroyPipeline(device,pipelines[i],NULL); vkDestroyShaderModule(device,shaders[i],NULL);
        vkDestroyImage(device,images[i],NULL); vkFreeMemory(device,image_memory[i],NULL);
        vkUnmapMemory(device,buffer_memory[i]); vkDestroyBuffer(device,buffers[i],NULL); vkFreeMemory(device,buffer_memory[i],NULL);
    }
    vkDestroyDevice(device,NULL); vkDestroyInstance(instance,NULL); dlclose(library);
    return sample_bad || resolve_bad;
}
