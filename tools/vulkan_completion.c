#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <hardware/hwvulkan.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

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
count_fds(void)
{
   DIR *dir = opendir("/proc/self/fd");
   if (!dir) return -1;
   int count = 0;
   struct dirent *entry;
   while ((entry = readdir(dir)))
      count += entry->d_name[0] != '.';
   closedir(dir);
   return count;
}

static int
compare_u64(const void *a, const void *b)
{
   uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
   return (x > y) - (x < y);
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
      .pApplicationName = "g2-completion", .apiVersion = VK_API_VERSION_1_1};
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
   LOAD(vkCreateSemaphore); LOAD(vkDestroySemaphore); LOAD(vkGetSemaphoreFdKHR);
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
   const char *extension = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
      .enabledExtensionCount = 1, .ppEnabledExtensionNames = &extension};
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
   uint64_t *notify_timings = calloc(frames, sizeof(*notify_timings));
   unsigned idle_us = getenv("MALI_G2_COMPLETION_IDLE_US") ? strtoul(getenv("MALI_G2_COMPLETION_IDLE_US"), NULL, 10) : 0;
   if (idle_us > 1000000) return 2;
   unsigned pending_fds = 0;
   const bool burst = getenv("MALI_G2_COMPLETION_BURST") != NULL;
   const bool empty_gaps = getenv("MALI_G2_COMPLETION_EMPTY_GAPS") != NULL;
   int fd_start = -1;
   if (!commands || !timings || !gpu_timings || !notify_timings) return 1;
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
   VkFence *gap_fences = calloc(count, sizeof(*gap_fences));
   if (!gap_fences) return 1;
   for (unsigned i = 0; empty_gaps && i < count; i++) CHECK(vkCreateFence(device, &fci, NULL, &gap_fences[i]));
   VkFence *burst_fences = calloc(count, sizeof(*burst_fences));
   if (!burst_fences) return 1;
   for (unsigned i = 0; burst && i + 1 < count; i++)
      CHECK(vkCreateFence(device, &fci, NULL, &burst_fences[i]));
   CHECK(vkCreateFence(device, &fci, NULL, &fence));
   VkExportSemaphoreCreateInfo export_info = {.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
   VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &export_info};
   VkSemaphore semaphore;
   CHECK(vkCreateSemaphore(device, &sci, NULL, &semaphore));
   printf("COMPLETION_TEST separate_native_semaphore_and_host_fence idle_us=%u\n", idle_us);
   for (uint32_t frame = 0; frame < frames + 8; frame++) {
      if (idle_us) {
         struct timespec gap = {.tv_sec = idle_us / 1000000, .tv_nsec = (idle_us % 1000000) * 1000};
         nanosleep(&gap, NULL);
      }
      for (uint32_t j = 0; j < bytes / 4; j++)
         mapped[j] = (frame + 17) * 0x123459u ^ (j * 0x1020301u);
      CHECK(vkResetFences(device, 1, &fence));
      if (empty_gaps) CHECK(vkResetFences(device, count, gap_fences));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = count, .pCommandBuffers = commands,
         .signalSemaphoreCount = 1, .pSignalSemaphores = &semaphore};
      uint64_t start = now_ns();
      if (burst) {
         if (count > 1) CHECK(vkResetFences(device, count - 1, burst_fences));
         for (unsigned i = 0; i < count; i++) {
            VkSubmitInfo one = submit;
            one.commandBufferCount = 1;
            one.pCommandBuffers = &commands[i];
            if (i + 1 < count) one.signalSemaphoreCount = 0;
            CHECK(vkQueueSubmit(queue, 1, &one, i + 1 < count ? burst_fences[i] : fence));
            if (empty_gaps) {
               VkSubmitInfo gap = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
               CHECK(vkQueueSubmit(queue, 1, &gap, gap_fences[i]));
            }
         }
      } else {
         CHECK(vkQueueSubmit(queue, 1, &submit, fence));
         if (empty_gaps) {
            VkSubmitInfo gap = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};
            CHECK(vkQueueSubmit(queue, 1, &gap, gap_fences[0]));
         }
      }
      VkSemaphoreGetFdInfoKHR fd_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
         .semaphore = semaphore, .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT};
      int fd = -1;
      CHECK(vkGetSemaphoreFdKHR(device, &fd_info, &fd));
      if (fd >= 0) {
         struct pollfd pfd = {.fd = fd, .events = POLLIN};
         if (frame >= 8 && poll(&pfd, 1, 0) == 0) pending_fds++;
         int ret;
         do { ret = poll(&pfd, 1, 10000); } while (ret < 0 && errno == EINTR);
         if (ret <= 0 || !(pfd.revents & POLLIN)) return 1;
         close(fd);
      }
      uint64_t native_seen = now_ns();
      CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
      if (frame >= 8) notify_timings[frame - 8] = now_ns() - native_seen;
      if (burst && count > 1) CHECK(vkWaitForFences(device, count - 1, burst_fences, VK_TRUE, UINT64_C(10000000000)));
      if (empty_gaps) CHECK(vkWaitForFences(device, burst ? count : 1, gap_fences, VK_TRUE, UINT64_C(10000000000)));
      if (frame >= 8) timings[frame - 8] = now_ns() - start;
      if (frame == 7) fd_start = count_fds();
      uint64_t stamps[2];
      CHECK(vkGetQueryPoolResults(device, query, 0, 2, sizeof(stamps), stamps,
         sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
      if (stamps[1] < stamps[0]) return 1;
      if (frame >= 8)
         gpu_timings[frame - 8] = (stamps[1] - stamps[0]) * properties.limits.timestampPeriod;
      for (uint32_t i = 1; i <= count; i++) {
         if (memcmp(mapped, (uint8_t *)mapped + (size_t)i * bytes, bytes)) {
            printf("COMPLETION_FAIL frame=%u command=%u\n", frame, i);
            return 1;
         }
      }
   }
   CHECK(vkDeviceWaitIdle(device));
   int fd_end = count_fds();
   printf("COMPLETION_FDS start=%d end=%d burst=%d empty_gaps=%d\n", fd_start, fd_end, burst, empty_gaps);
   if (fd_start < 0 || fd_end < 0 || fd_end > fd_start + 2) {
      fprintf(stderr, "completion fd growth\n"); return 1;
   }
   qsort(timings, frames, sizeof(*timings), compare_u64);
   qsort(gpu_timings, frames, sizeof(*gpu_timings), compare_u64);
   qsort(notify_timings, frames, sizeof(*notify_timings), compare_u64);
   printf("COMPLETION_PASS idle_us=%u pending_fds=%u notify_p50_us=%.3f notify_p95_us=%.3f notify_max_us=%.3f ",
      idle_us, pending_fds, notify_timings[frames / 2] / 1000.0,
      notify_timings[frames * 95 / 100] / 1000.0, notify_timings[frames - 1] / 1000.0);
   printf("SUBMIT_PASS commands=%u frames=%u bytes=%u median_us=%.3f p95_us=%.3f gpu_median_us=%.3f gpu_p95_us=%.3f\n",
      count, frames, bytes, timings[frames / 2] / 1000.0,
      timings[(frames * 95) / 100] / 1000.0, gpu_timings[frames / 2] / 1000.0,
      gpu_timings[(frames * 95) / 100] / 1000.0);
   CHECK(vkDeviceWaitIdle(device));
   for (unsigned i = 0; empty_gaps && i < count; i++) vkDestroyFence(device, gap_fences[i], NULL);
   free(gap_fences);
   vkDestroySemaphore(device, semaphore, NULL);
   for (unsigned i = 0; burst && i + 1 < count; i++) vkDestroyFence(device, burst_fences[i], NULL);
   free(burst_fences);
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
   free(notify_timings);
   dlclose(library);
   return 0;
}
