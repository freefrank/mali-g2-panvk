#define VK_NO_PROTOTYPES
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

static int
compare_u64(const void *a, const void *b)
{
   uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
   return (x > y) - (x < y);
}

static int
scan_flushes(unsigned minimum_ends)
{
   FILE *maps = fopen("/proc/self/maps", "r");
   if (!maps) return 1;
   char line[2048], perms[5];
   unsigned rings = 0, total_flushes = 0;
   while (fgets(line, sizeof(line), maps)) {
      unsigned long start, end;
      if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3 ||
          perms[0] != 'r' || perms[1] != 'w' || end - start != 65536 ||
          !strstr(line, "/dev/mali0")) continue;
      const uint64_t *words = (const uint64_t *)(uintptr_t)start;
      unsigned ends = 0, flushes = 0;
      for (unsigned i = 0; i < 65536 / 8; i++) {
         flushes += words[i] >> 56 == 36;
         if (i + 4 < 65536 / 8 && words[i] == UINT64_C(47) << 56 &&
             words[i + 1] >> 56 == 1 && words[i + 2] >> 56 == 1 &&
             (words[i + 2] & UINT64_C(0xffffffffffff)) == 1 &&
             words[i + 3] >> 56 == 51 && words[i + 4] == UINT64_C(3) << 56)
            ends++;
      }
      if (ends < minimum_ends) continue;
      printf("FLUSH_RING index=%u ends=%u flushes=%u\n", rings, ends, flushes);
      rings++;
      total_flushes += flushes;
   }
   fclose(maps);
   printf("FLUSH_SCAN rings=%u flushes=%u\n", rings, total_flushes);
   return rings != 3;
}

int
main(int argc, char **argv)
{
   setbuf(stdout, NULL);
   if (argc != 5) return 2;
   uint32_t count = strtoul(argv[2], NULL, 10);
   uint32_t frames = strtoul(argv[3], NULL, 10);
   uint32_t bytes = strtoul(argv[4], NULL, 10);
   if (!count || count > 128 || frames < 16 || frames > 4096 ||
       !bytes || bytes > 1048576 || bytes % 4) return 2;
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
      .pApplicationName = "g2-submit", .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app};
   VkInstance instance;
   CHECK(create(&ici, NULL, &instance));
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
   LOAD(vkCreateQueryPool); LOAD(vkDestroyQueryPool);
   LOAD(vkCmdResetQueryPool); LOAD(vkCmdWriteTimestamp); LOAD(vkGetQueryPoolResults);
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
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
   VkDevice device;
   CHECK(vkCreateDevice(physical, &dci, NULL, &device));
   VkQueue queue;
   vkGetDeviceQueue(device, family, 0, &queue);
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = (VkDeviceSize)(count + 1) * bytes,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
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
   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = family};
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(device, &pci, NULL, &pool));
   VkCommandBuffer *commands = calloc(count, sizeof(*commands));
   uint64_t *timings = calloc(frames, sizeof(*timings));
   uint64_t *gpu_timings = calloc(frames, sizeof(*gpu_timings));
   if (!commands || !timings || !gpu_timings) return 1;
   VkQueryPoolCreateInfo query_info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2};
   VkQueryPool query;
   CHECK(vkCreateQueryPool(device, &query_info, NULL, &query));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = count};
   CHECK(vkAllocateCommandBuffers(device, &cai, commands));
   for (uint32_t i = 0; i < count; i++) {
      VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      CHECK(vkBeginCommandBuffer(commands[i], &begin));
      if (i == 0) {
         vkCmdResetQueryPool(commands[i], query, 0, 2);
         vkCmdWriteTimestamp(commands[i], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query, 0);
      }
      VkMemoryBarrier dependency = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
      vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &dependency, 0, NULL, 0, NULL);
      VkBufferCopy region = {.srcOffset = (VkDeviceSize)i * bytes,
         .dstOffset = (VkDeviceSize)(i + 1) * bytes, .size = bytes};
      vkCmdCopyBuffer(commands[i], buffer, buffer, 1, &region);
      if (i + 1 == count) {
         dependency.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
         vkCmdPipelineBarrier(commands[i], VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &dependency, 0, NULL, 0, NULL);
         vkCmdWriteTimestamp(commands[i], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query, 1);
      }
      CHECK(vkEndCommandBuffer(commands[i]));
   }
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CHECK(vkCreateFence(device, &fci, NULL, &fence));
   for (uint32_t frame = 0; frame < frames + 8; frame++) {
      for (uint32_t j = 0; j < bytes / 4; j++)
         mapped[j] = (frame + 17) * 0x123459u ^ (j * 0x1020301u);
      CHECK(vkResetFences(device, 1, &fence));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = count, .pCommandBuffers = commands};
      uint64_t start = now_ns();
      CHECK(vkQueueSubmit(queue, 1, &submit, fence));
      CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
      if (frame >= 8) timings[frame - 8] = now_ns() - start;
      uint64_t stamps[2];
      CHECK(vkGetQueryPoolResults(device, query, 0, 2, sizeof(stamps), stamps,
         sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      if (stamps[1] < stamps[0]) return 1;
      if (frame >= 8)
         gpu_timings[frame - 8] = (stamps[1] - stamps[0]) * properties.limits.timestampPeriod;
      for (uint32_t i = 1; i <= count; i++) {
         if (memcmp(mapped, (uint8_t *)mapped + (size_t)i * bytes, bytes)) {
            printf("SUBMIT_FAIL frame=%u command=%u\n", frame, i);
            return 1;
         }
      }
   }
   qsort(timings, frames, sizeof(*timings), compare_u64);
   qsort(gpu_timings, frames, sizeof(*gpu_timings), compare_u64);
   printf("SUBMIT_PASS commands=%u frames=%u bytes=%u median_us=%.3f p95_us=%.3f gpu_median_us=%.3f gpu_p95_us=%.3f\n",
      count, frames, bytes, timings[frames / 2] / 1000.0,
      timings[(frames * 95) / 100] / 1000.0, gpu_timings[frames / 2] / 1000.0,
      gpu_timings[(frames * 95) / 100] / 1000.0);
   CHECK(vkDeviceWaitIdle(device));
   if (getenv("MALI_G2_SUBMIT_SCAN_FLUSH") && scan_flushes(frames + 8)) return 1;
   vkDestroyFence(device, fence, NULL);
   vkDestroyCommandPool(device, pool, NULL);
   vkDestroyQueryPool(device, query, NULL);
   vkUnmapMemory(device, memory);
   vkDestroyBuffer(device, buffer, NULL);
   vkFreeMemory(device, memory, NULL);
   vkDestroyDevice(device, NULL);
   vkDestroyInstance(instance, NULL);
   free(commands);
   free(timings);
   free(gpu_timings);
   dlclose(library);
   return 0;
}
