#ifndef __TARGET_H_INCLUDED__
#define __TARGET_H_INCLUDED__

#include "core/Device.h"

struct Target {
  VkImage        handle      = VK_NULL_HANDLE;
  VkDeviceMemory memory      = VK_NULL_HANDLE;
  VkImageView    view        = VK_NULL_HANDLE;
  VkImage        depth       = VK_NULL_HANDLE;
  VkDeviceMemory depthMemory = VK_NULL_HANDLE;
  VkImageView    depthView   = VK_NULL_HANDLE;
  std::uint32_t  width       = 0;
  std::uint32_t  height      = 0;
};

inline constexpr VkFormat TARGET_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
inline constexpr VkFormat DEPTH_FORMAT  = VK_FORMAT_D32_SFLOAT;

Target createTarget(const Device& dev, std::uint32_t width, std::uint32_t height,
                    bool withDepth = false);
void   destroyTarget(const Device& dev, Target& target);

std::vector<std::uint8_t> readTarget(const Device& dev, const Target& target);

void createImage(const Device& dev, std::uint32_t width, std::uint32_t height,
                 std::uint32_t levels, VkFormat format, VkImageUsageFlags usage,
                 VkImageAspectFlags aspect, VkImageCreateFlags flags,
                 VkImage& image, VkDeviceMemory& memory, VkImageView& view);

VkImageView createView(const Device& dev, VkImage image, VkFormat format,
                       VkImageAspectFlags aspect, std::uint32_t levels);

#endif
