#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vk_android_native_buffer.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
   fprintf(stderr, "%s failed: %d\n", #call, r); return 1; } } while (0)
#define LOAD(name) PFN_##name name = (PFN_##name)get(instance, #name); \
   if (!name) { fprintf(stderr, "%s missing\n", #name); return 1; }

static uint64_t
now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}


struct consumer_args {
 PFN_vkQueueSubmit submit; VkQueue queue; VkCommandBuffer command;
 VkSemaphore semaphore; VkFence fence; atomic_int started; VkResult result;
};
static void *consume(void *data) {
 struct consumer_args *a=data;
 VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
 VkSubmitInfo info={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,
  .pWaitSemaphores=&a->semaphore,.pWaitDstStageMask=&stage,.commandBufferCount=1,.pCommandBuffers=&a->command};
 atomic_store(&a->started,1);a->result=a->submit(a->queue,1,&info,a->fence);return NULL;
}

int
main(int argc, char **argv)
{
   setbuf(stdout, NULL);
   if (argc != 4) return 2;
   const char *mode=argv[3];
   int want_sem=!strcmp(mode,"semaphore") || !strcmp(mode,"both");
   int want_fence=!strcmp(mode,"fence") || !strcmp(mode,"both");
   int minusone=!strcmp(mode,"minusone");
   int signaled=!strcmp(mode,"signaled") || minusone;
   if (signaled) want_sem=want_fence=1;
   if (!want_sem && !want_fence && strcmp(mode,"none")) return 2;
   const uint32_t count=1, bytes=1024;
   void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr get = dlsym(library, "vk_icdGetInstanceProcAddr");
   if (!get) get = dlsym(library, "vkGetInstanceProcAddr");
   PFN_vkCreateInstance create = NULL;
   if (!get) {
      hwvulkan_module_t *module = dlsym(library, HAL_MODULE_INFO_SYM_AS_STR);
      hwvulkan_device_t *hal = NULL;
      if (!module || module->common.methods->open(&module->common, HWVULKAN_DEVICE_0,
                                                (hw_device_t **)&hal)) return 1;
      get = hal->GetInstanceProcAddr;
      create = hal->CreateInstance;
   }
   if (!create) create = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "g2-acquire", .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app};
   VkInstance instance;
   CHECK(create(&ici, NULL, &instance));
   LOAD(vkAcquireImageANDROID); LOAD(vkGetFenceFdKHR); LOAD(vkGetFenceStatus); LOAD(vkGetSemaphoreFdKHR);
   LOAD(vkCreateSemaphore); LOAD(vkDestroySemaphore);
   LOAD(vkCreateShaderModule); LOAD(vkDestroyShaderModule);
   LOAD(vkCreateDescriptorSetLayout); LOAD(vkDestroyDescriptorSetLayout);
   LOAD(vkCreatePipelineLayout); LOAD(vkDestroyPipelineLayout);
   LOAD(vkCreateComputePipelines); LOAD(vkDestroyPipeline);
   LOAD(vkCreateDescriptorPool); LOAD(vkDestroyDescriptorPool);
   LOAD(vkAllocateDescriptorSets); LOAD(vkUpdateDescriptorSets);
   LOAD(vkCmdBindPipeline); LOAD(vkCmdBindDescriptorSets); LOAD(vkCmdPushConstants); LOAD(vkCmdDispatch);
   LOAD(vkCmdFillBuffer); LOAD(vkCreateImage); LOAD(vkDestroyImage);
   LOAD(vkGetImageMemoryRequirements); LOAD(vkBindImageMemory);
   LOAD(vkEnumeratePhysicalDevices);
   LOAD(vkGetPhysicalDeviceQueueFamilyProperties);
   LOAD(vkGetPhysicalDeviceMemoryProperties);
   LOAD(vkGetPhysicalDeviceProperties);
   LOAD(vkCreateDevice); LOAD(vkGetDeviceQueue);
   LOAD(vkCreateBuffer); LOAD(vkGetBufferMemoryRequirements);
   LOAD(vkAllocateMemory); LOAD(vkBindBufferMemory); LOAD(vkMapMemory);
   LOAD(vkCreateCommandPool); LOAD(vkAllocateCommandBuffers);
   LOAD(vkBeginCommandBuffer); LOAD(vkEndCommandBuffer);
   LOAD(vkCmdCopyBuffer); LOAD(vkCmdPipelineBarrier);

   LOAD(vkCreateFence); LOAD(vkResetFences); LOAD(vkWaitForFences);
   LOAD(vkQueueSubmit); LOAD(vkDeviceWaitIdle);
   LOAD(vkDestroyFence); LOAD(vkDestroyCommandPool);
   LOAD(vkUnmapMemory); LOAD(vkDestroyBuffer); LOAD(vkFreeMemory);
   LOAD(vkDestroyDevice); LOAD(vkDestroyInstance);
   uint32_t n = 1;
   VkPhysicalDevice physical;
   CHECK(vkEnumeratePhysicalDevices(instance, &n, &physical));
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, NULL);
   VkQueueFamilyProperties *families = calloc(n, sizeof(*families));
   if (!families) return 1;
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families);
   uint32_t family = 0;
   while (family < n && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) family++;
   free(families);
   if (family == n) return 1;
   VkPhysicalDeviceProperties properties;
   vkGetPhysicalDeviceProperties(physical, &properties);
   float priorities[2]={1,1};
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = priorities};
   const char *extensions[]={VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,VK_ANDROID_NATIVE_BUFFER_EXTENSION_NAME};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
      .enabledExtensionCount=3,.ppEnabledExtensionNames=extensions};
   VkDevice device;
   CHECK(vkCreateDevice(physical, &dci, NULL, &device));
   VkDevice acquire_device;
   CHECK(vkCreateDevice(physical, &dci, NULL, &acquire_device));
   printf("ACQUIRE_TEST devices=2 real_sync_fd\n");
   VkQueue queue;
   vkGetDeviceQueue(device, family, 0, &queue);
   VkQueue consumer_queue;vkGetDeviceQueue(acquire_device,family,0,&consumer_queue);
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = (VkDeviceSize)(count + 1) * bytes,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
   VkBuffer buffer;
   CHECK(vkCreateBuffer(device, &bci, NULL, &buffer));
   VkMemoryRequirements mr;
   vkGetBufferMemoryRequirements(device, buffer, &mr);
   VkPhysicalDeviceMemoryProperties mp;
   vkGetPhysicalDeviceMemoryProperties(physical, &mp);
   uint32_t type = 0;
   VkMemoryPropertyFlags required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   while (type < mp.memoryTypeCount && (!(mr.memoryTypeBits & (1u << type)) ||
          (mp.memoryTypes[type].propertyFlags & required) != required)) type++;
   if (type == mp.memoryTypeCount) return 1;
   VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size, .memoryTypeIndex = type};
   VkDeviceMemory memory;
   CHECK(vkAllocateMemory(device, &ai, NULL, &memory));
   CHECK(vkBindBufferMemory(device, buffer, memory, 0));
   uint32_t *mapped;
   CHECK(vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
   VkBuffer consumer_buffer;
   CHECK(vkCreateBuffer(acquire_device,&bci,NULL,&consumer_buffer));
   vkGetBufferMemoryRequirements(acquire_device,consumer_buffer,&mr);
   ai.allocationSize=mr.size;
   type=0;while(type<mp.memoryTypeCount && (!(mr.memoryTypeBits&(1u<<type)) ||
      (mp.memoryTypes[type].propertyFlags&required)!=required))type++;
   if(type==mp.memoryTypeCount)return 1;ai.memoryTypeIndex=type;
   VkDeviceMemory consumer_memory;
   CHECK(vkAllocateMemory(acquire_device,&ai,NULL,&consumer_memory));
   CHECK(vkBindBufferMemory(acquire_device,consumer_buffer,consumer_memory,0));
   uint32_t *consumer_mapped;
   CHECK(vkMapMemory(acquire_device,consumer_memory,0,VK_WHOLE_SIZE,0,(void **)&consumer_mapped));
   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family};
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
   VkCommandPool consumer_pool;
   CHECK(vkCreateCommandPool(acquire_device,&pci,NULL,&consumer_pool));
   FILE *f=fopen(argv[2],"rb");if(!f)return 1;
   fseek(f,0,SEEK_END);long shader_size=ftell(f);rewind(f);
   uint32_t *code=malloc(shader_size);if(!code || fread(code,1,shader_size,f)!=(size_t)shader_size)return 1;fclose(f);
   VkShaderModuleCreateInfo smi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=shader_size,.pCode=code};
   VkShaderModule shader;CHECK(vkCreateShaderModule(device,&smi,NULL,&shader));free(code);
   VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo sli={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
   VkDescriptorSetLayout set_layout;CHECK(vkCreateDescriptorSetLayout(device,&sli,NULL,&set_layout));
   VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=4};
   VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
   VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&pli,NULL,&layout));
   VkComputePipelineCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
    .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"}};
   VkPipeline pipeline;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cpi,NULL,&pipeline));
   VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
   VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
   VkDescriptorPool descriptor_pool;CHECK(vkCreateDescriptorPool(device,&dpi,NULL,&descriptor_pool));
   VkDescriptorSetAllocateInfo dsi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&dsi,&set));
   VkDescriptorBufferInfo dbi={buffer,0,4};
   VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&dbi};
   vkUpdateDescriptorSets(device,1,&write,0,NULL);
   VkImageCreateInfo ici2={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.extent={16,16,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT};
   VkImage image;CHECK(vkCreateImage(acquire_device,&ici2,NULL,&image));vkGetImageMemoryRequirements(acquire_device,image,&mr);
   ai.allocationSize=mr.size;type=0;while(type<mp.memoryTypeCount && !(mr.memoryTypeBits&(1u<<type)))type++;ai.memoryTypeIndex=type;
   VkDeviceMemory image_memory;CHECK(vkAllocateMemory(acquire_device,&ai,NULL,&image_memory));CHECK(vkBindImageMemory(acquire_device,image,image_memory,0));
   VkCommandBuffer commands[2];
   VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   CHECK(vkAllocateCommandBuffers(device,&cai,&commands[0]));
   cai.commandPool=consumer_pool;CHECK(vkAllocateCommandBuffers(acquire_device,&cai,&commands[1]));
   uint32_t iterations=8000000;
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CHECK(vkBeginCommandBuffer(commands[0],&begin));
   vkCmdBindPipeline(commands[0],VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
   vkCmdBindDescriptorSets(commands[0],VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
   vkCmdPushConstants(commands[0],layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&iterations);vkCmdDispatch(commands[0],1,1,1);
   VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(commands[0],VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);CHECK(vkEndCommandBuffer(commands[0]));
   CHECK(vkBeginCommandBuffer(commands[1],&begin));vkCmdFillBuffer(commands[1],consumer_buffer,1024,1024,0x13572468);
   host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
   vkCmdPipelineBarrier(commands[1],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);CHECK(vkEndCommandBuffer(commands[1]));
   VkExportFenceCreateInfo export_info={.sType=VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,.handleTypes=VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT};
   VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.pNext=&export_info};VkFence producer;
   CHECK(vkCreateFence(device,&fci,NULL,&producer));fci.pNext=NULL;VkFence acquired,consumer;
   CHECK(vkCreateFence(acquire_device,&fci,NULL,&acquired));CHECK(vkCreateFence(acquire_device,&fci,NULL,&consumer));
   VkExportSemaphoreCreateInfo sem_export={.sType=VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,.handleTypes=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
   VkSemaphoreCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&sem_export};VkSemaphore semaphore;CHECK(vkCreateSemaphore(acquire_device,&sci,NULL,&semaphore));
   memset(mapped,0xff,2048);
   memset(consumer_mapped,0xff,2048);
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&commands[0]};CHECK(vkQueueSubmit(queue,1,&submit,producer));
   VkFenceGetFdInfoKHR get_fd={.sType=VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR,.fence=producer,.handleType=VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT};int fd=-1;CHECK(vkGetFenceFdKHR(device,&get_fd,&fd));
   if(fd<0){fprintf(stderr,"native fence already signaled before export\n");return 1;}
   int observer=dup(fd);if(observer<0)return 1;
   struct pollfd pending={.fd=observer,.events=POLLIN};
   if(poll(&pending,1,0)!=0){fprintf(stderr,"producer finished before import\n");return 1;}
   if(signaled && poll(&pending,1,5000)<=0)return 1;
   if(minusone){close(fd);fd=-1;}
   uint64_t start=now_ns();CHECK(vkAcquireImageANDROID(acquire_device,image,fd,want_sem?semaphore:VK_NULL_HANDLE,want_fence?acquired:VK_NULL_HANDLE));
   double acquire_ms=(now_ns()-start)/1000000.0;int pending_after=poll(&pending,1,0)==0;
   if(want_fence && vkGetFenceStatus(acquire_device,acquired)==VK_SUCCESS && poll(&pending,1,0)==0){fprintf(stderr,"acquire fence signaled early\n");return 1;}
   int semaphore_pending_observed=0;
   if(want_sem){
    VkSemaphoreGetFdInfoKHR si={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,.semaphore=semaphore,.handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
    int sf=-1;CHECK(vkGetSemaphoreFdKHR(acquire_device,&si,&sf));
    struct pollfd sp={.fd=sf,.events=POLLIN};
    int ready=sf<0 || poll(&sp,1,0)>0;
    if(ready && poll(&pending,1,0)==0){fprintf(stderr,"acquire semaphore signaled early\n");return 1;}
    semaphore_pending_observed=!ready && poll(&pending,1,0)==0;
    int restore=sf<0?-1:dup(sf);if(sf>=0 && restore<0)return 1;
    CHECK(vkAcquireImageANDROID(acquire_device,image,restore,semaphore,VK_NULL_HANDLE));
    if(sf>=0)close(sf);
   }
   struct consumer_args a={.submit=vkQueueSubmit,.queue=consumer_queue,.command=commands[1],.semaphore=semaphore,.fence=consumer,.result=VK_NOT_READY};
   atomic_init(&a.started,0);pthread_t worker;
   if(want_sem){
    if(pthread_create(&worker,NULL,consume,&a))return 1;
    struct timespec brief={.tv_nsec=5000000};nanosleep(&brief,NULL);
    if(vkGetFenceStatus(acquire_device,consumer)==VK_SUCCESS && poll(&pending,1,0)==0){fprintf(stderr,"consumer completed before native fence\n");return 1;}
   }
   if(poll(&pending,1,5000)<=0){fprintf(stderr,"producer timeout\n");return 1;}
   if(want_fence)CHECK(vkWaitForFences(acquire_device,1,&acquired,VK_TRUE,UINT64_C(5000000000)));
   if(want_sem){pthread_join(worker,NULL);CHECK(a.result);CHECK(vkWaitForFences(acquire_device,1,&consumer,VK_TRUE,UINT64_C(5000000000)));for(unsigned i=256;i<512;i++)if(consumer_mapped[i]!=0x13572468)return 1;}
   CHECK(vkDeviceWaitIdle(device));
   CHECK(vkDeviceWaitIdle(acquire_device));
   uint32_t expected=1;for(uint32_t i=0;i<iterations;i++)expected=(expected*1664525u+1013904223u)^(expected>>13);
   if(mapped[0]!=expected){fprintf(stderr,"producer result got=%#x expected=%#x\n",mapped[0],expected);return 1;}
   printf("ACQUIRE_PASS mode=%s acquire_ms=%.3f native_pending_after_return=%d semaphore_pending_observed=%d ordering=ok\n",mode,acquire_ms,pending_after,semaphore_pending_observed);
   close(observer);vkDestroySemaphore(acquire_device,semaphore,NULL);vkDestroyFence(device,producer,NULL);vkDestroyFence(acquire_device,acquired,NULL);vkDestroyFence(acquire_device,consumer,NULL);
   vkDestroyImage(acquire_device,image,NULL);vkFreeMemory(acquire_device,image_memory,NULL);vkDestroyDescriptorPool(device,descriptor_pool,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorSetLayout(device,set_layout,NULL);vkDestroyShaderModule(device,shader,NULL);
   vkDestroyCommandPool(acquire_device,consumer_pool,NULL);vkUnmapMemory(acquire_device,consumer_memory);vkDestroyBuffer(acquire_device,consumer_buffer,NULL);vkFreeMemory(acquire_device,consumer_memory,NULL);vkDestroyDevice(acquire_device,NULL);
   vkDestroyCommandPool(device,pool,NULL);vkUnmapMemory(device,memory);vkDestroyBuffer(device,buffer,NULL);vkFreeMemory(device,memory,NULL);vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);dlclose(library);return 0;
}
