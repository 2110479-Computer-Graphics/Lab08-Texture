#ifndef __DEVICE_H_INCLUDED__
#define __DEVICE_H_INCLUDED__

#include "utils/utils.h"

#include <functional>
#include <string>

struct GLFWwindow;

struct Device {
  VkInstance       instance = VK_NULL_HANDLE;
  VkSurfaceKHR     surface  = VK_NULL_HANDLE;
  VkPhysicalDevice gpu      = VK_NULL_HANDLE;
  VkDevice         device   = VK_NULL_HANDLE;
  VkQueue          gfxQueue = VK_NULL_HANDLE;
  std::uint32_t    gfxFamily = 0;

  VkCommandPool   cmdPool = VK_NULL_HANDLE;
  VkCommandBuffer cmd     = VK_NULL_HANDLE;
  VkQueryPool     queries = VK_NULL_HANDLE;

  VkPhysicalDeviceProperties props{};
  std::string   driverName;
  std::string   driverInfo;
  std::uint32_t subgroupSize        = 0;
  bool          hasTimestamps       = false;
  bool          hasPipelineStats    = false;
  bool          hasAnisotropy       = false;
  float         maxAnisotropy       = 1.0f;
  bool          hasIndependentBlend = false;
};

Device createDevice(GLFWwindow* window);
void   destroyDevice(Device& dev);

std::uint32_t findMemoryType(const Device& dev, std::uint32_t typeFilter, VkMemoryPropertyFlags props);

void submitNow(const Device& dev, const std::function<void(VkCommandBuffer)>& record);

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout from, VkImageLayout to,
                     std::uint32_t baseLevel = 0, std::uint32_t levelCount = VK_REMAINING_MIP_LEVELS);

void hostReadBarrier(VkCommandBuffer cmd);

#endif
