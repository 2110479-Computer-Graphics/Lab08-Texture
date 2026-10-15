#include "core/Device.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

static std::vector<VkExtensionProperties> instanceExts() {
  std::uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> exts(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
  return exts;
}

static std::vector<VkExtensionProperties> deviceExts(VkPhysicalDevice gpu) {
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> exts(count);
  vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, exts.data());
  return exts;
}

static bool hasExt(const std::vector<VkExtensionProperties>& exts, const char* name) {
  return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& e) {
      return std::strcmp(e.extensionName, name) == 0;
  });
}

static bool hasValidationLayers() {
  std::uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  return std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& l) {
      return std::strcmp(l.layerName, VALIDATION_LAYERS[0]) == 0;
  });
}

static void createInstance(Device& dev, bool wantSurface) {
  std::vector<VkExtensionProperties> available = instanceExts();

  std::vector<const char*> exts;
  if (wantSurface) {
    std::uint32_t glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    for (std::uint32_t i = 0; i < glfwCount; ++i)
      exts.push_back(glfwExts[i]);
  }
  VkInstanceCreateFlags    flags = 0;
  if (hasExt(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  std::vector<const char*> layers;
  if (ENABLE_VALIDATION && hasValidationLayers()) layers = VALIDATION_LAYERS;

  VkApplicationInfo app{};
  app.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "lab08";
  app.apiVersion       = VK_API_VERSION_1_3;

  VkInstanceCreateInfo instanceCI{};
  instanceCI.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceCI.flags                   = flags;
  instanceCI.pApplicationInfo        = &app;
  instanceCI.enabledExtensionCount   = static_cast<std::uint32_t>(exts.size());
  instanceCI.ppEnabledExtensionNames = exts.data();
  instanceCI.enabledLayerCount       = static_cast<std::uint32_t>(layers.size());
  instanceCI.ppEnabledLayerNames     = layers.data();

  check(vkCreateInstance(&instanceCI, nullptr, &dev.instance), "vkCreateInstance");
}

static void pickGPU(Device& dev) {
  std::uint32_t count = 0;
  vkEnumeratePhysicalDevices(dev.instance, &count, nullptr);
  if (count == 0)
    throw std::runtime_error("no Vulkan device found, see 'If it does not work' in the handout");

  std::vector<VkPhysicalDevice> gpus(count);
  vkEnumeratePhysicalDevices(dev.instance, &count, gpus.data());

  dev.gpu = gpus[0];
  for (VkPhysicalDevice gpu : gpus) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu, &props);
    if (props.limits.timestampComputeAndGraphics) {
      dev.gpu = gpu;
      break;
    }
  }

  VkPhysicalDeviceSubgroupProperties subgroup{};
  subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

  VkPhysicalDeviceDriverProperties driver{};
  driver.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
  subgroup.pNext = &driver;

  VkPhysicalDeviceProperties2 props2{};
  props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  props2.pNext = &subgroup;
  vkGetPhysicalDeviceProperties2(dev.gpu, &props2);

  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceFeatures(dev.gpu, &features);

  dev.props               = props2.properties;
  dev.driverName          = driver.driverName;
  dev.driverInfo          = driver.driverInfo;
  dev.subgroupSize        = subgroup.subgroupSize;
  dev.hasTimestamps       = dev.props.limits.timestampComputeAndGraphics != 0;
  dev.hasPipelineStats    = features.pipelineStatisticsQuery != 0;
  dev.hasAnisotropy       = features.samplerAnisotropy != 0;
  dev.maxAnisotropy       = dev.hasAnisotropy ? dev.props.limits.maxSamplerAnisotropy : 1.0f;
  dev.hasIndependentBlend = features.independentBlend != 0;
}

static void createLogicalDevice(Device& dev) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(dev.gpu, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(dev.gpu, &count, families.data());

  for (std::uint32_t i = 0; i < count; ++i)
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      dev.gfxFamily = i;
      break;
    }

  std::vector<const char*> exts;
  if (hasExt(deviceExts(dev.gpu), "VK_KHR_portability_subset"))
    exts.push_back("VK_KHR_portability_subset");
  if (dev.surface != VK_NULL_HANDLE)
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  VkPhysicalDeviceFeatures enabled{};
  enabled.samplerAnisotropy = dev.hasAnisotropy ? VK_TRUE : VK_FALSE;
  enabled.independentBlend  = dev.hasIndependentBlend ? VK_TRUE : VK_FALSE;

  float priority = 1.0f;

  VkDeviceQueueCreateInfo queueCI{};
  queueCI.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCI.queueFamilyIndex = dev.gfxFamily;
  queueCI.queueCount       = 1;
  queueCI.pQueuePriorities = &priority;

  VkDeviceCreateInfo deviceCI{};
  deviceCI.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCI.queueCreateInfoCount    = 1;
  deviceCI.pQueueCreateInfos       = &queueCI;
  deviceCI.enabledExtensionCount   = static_cast<std::uint32_t>(exts.size());
  deviceCI.ppEnabledExtensionNames = exts.data();
  deviceCI.pEnabledFeatures        = &enabled;

  check(vkCreateDevice(dev.gpu, &deviceCI, nullptr, &dev.device), "vkCreateDevice");
  vkGetDeviceQueue(dev.device, dev.gfxFamily, 0, &dev.gfxQueue);
}

static void createCommands(Device& dev) {
  VkCommandPoolCreateInfo poolCI{};
  poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolCI.queueFamilyIndex = dev.gfxFamily;
  check(vkCreateCommandPool(dev.device, &poolCI, nullptr, &dev.cmdPool), "vkCreateCommandPool");

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool        = dev.cmdPool;
  allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  check(vkAllocateCommandBuffers(dev.device, &allocInfo, &dev.cmd), "vkAllocateCommandBuffers");

  VkQueryPoolCreateInfo queryCI{};
  queryCI.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryCI.queryType  = VK_QUERY_TYPE_TIMESTAMP;
  queryCI.queryCount = 2;
  check(vkCreateQueryPool(dev.device, &queryCI, nullptr, &dev.queries), "vkCreateQueryPool");
}

Device createDevice(GLFWwindow* window) {
  Device dev{};
  createInstance(dev, window != nullptr);
  if (window != nullptr)
    check(glfwCreateWindowSurface(dev.instance, window, nullptr, &dev.surface),
          "glfwCreateWindowSurface");
  pickGPU(dev);
  createLogicalDevice(dev);
  createCommands(dev);
  return dev;
}

void destroyDevice(Device& dev) {
  vkDestroyQueryPool(dev.device, dev.queries, nullptr);
  vkDestroyCommandPool(dev.device, dev.cmdPool, nullptr);
  vkDestroyDevice(dev.device, nullptr);
  if (dev.surface != VK_NULL_HANDLE)
    vkDestroySurfaceKHR(dev.instance, dev.surface, nullptr);
  vkDestroyInstance(dev.instance, nullptr);
  dev = Device{};
}

std::uint32_t findMemoryType(const Device& dev, std::uint32_t typeFilter, VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(dev.gpu, &memProps);
  for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
      return i;
  throw std::runtime_error("no suitable memory type");
}

void submitNow(const Device& dev, const std::function<void(VkCommandBuffer)>& record) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(dev.cmd, &beginInfo), "vkBeginCommandBuffer");
  record(dev.cmd);
  check(vkEndCommandBuffer(dev.cmd), "vkEndCommandBuffer");

  VkSubmitInfo submit{};
  submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers    = &dev.cmd;
  check(vkQueueSubmit(dev.gfxQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
  check(vkQueueWaitIdle(dev.gfxQueue), "vkQueueWaitIdle");
}

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to,
                     std::uint32_t baseLevel, std::uint32_t levelCount)
{
  VkImageMemoryBarrier barrier{};
  barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask       = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.oldLayout           = from;
  barrier.newLayout           = to;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image               = image;
  barrier.subresourceRange    = { aspect, baseLevel, levelCount, 0, 1 };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void hostReadBarrier(VkCommandBuffer cmd) {
  VkMemoryBarrier barrier{};
  barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);
}
