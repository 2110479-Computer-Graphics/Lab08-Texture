#include "core/Target.h"

#include "core/Buffer.h"

#include <cstring>

VkImageView createView(const Device& dev, VkImage image, VkFormat format,
                       VkImageAspectFlags aspect, std::uint32_t levels)
{
  VkImageViewCreateInfo viewCI{};
  viewCI.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCI.image            = image;
  viewCI.viewType         = VK_IMAGE_VIEW_TYPE_2D;
  viewCI.format           = format;
  viewCI.subresourceRange = { aspect, 0, levels, 0, 1 };

  VkImageView view = VK_NULL_HANDLE;
  check(vkCreateImageView(dev.device, &viewCI, nullptr, &view), "vkCreateImageView");
  return view;
}

void createImage(const Device& dev, std::uint32_t width, std::uint32_t height,
                 std::uint32_t levels, VkFormat format, VkImageUsageFlags usage,
                 VkImageAspectFlags aspect, VkImageCreateFlags flags,
                 VkImage& image, VkDeviceMemory& memory, VkImageView& view)
{
  VkImageCreateInfo imageCI{};
  imageCI.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.flags         = flags;
  imageCI.imageType     = VK_IMAGE_TYPE_2D;
  imageCI.format        = format;
  imageCI.extent        = { width, height, 1 };
  imageCI.mipLevels     = levels;
  imageCI.arrayLayers   = 1;
  imageCI.samples       = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling        = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage         = usage;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(vkCreateImage(dev.device, &imageCI, nullptr, &image), "vkCreateImage");

  VkMemoryRequirements memReq;
  vkGetImageMemoryRequirements(dev.device, image, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize  = memReq.size;
  allocInfo.memoryTypeIndex =
    findMemoryType(dev, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(dev.device, &allocInfo, nullptr, &memory), "vkAllocateMemory");
  check(vkBindImageMemory(dev.device, image, memory, 0), "vkBindImageMemory");

  view = createView(dev, image, format, aspect, levels);
}

Target createTarget(const Device& dev, std::uint32_t width, std::uint32_t height, bool withDepth) {
  Target target{};
  target.width  = width;
  target.height = height;

  createImage(dev, width, height, 1, TARGET_FORMAT,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, 0, target.handle, target.memory, target.view);

  if (withDepth)
    createImage(dev, width, height, 1, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT, 0, target.depth, target.depthMemory, target.depthView);

  submitNow(dev, [&](VkCommandBuffer cmd) {
    transitionImage(cmd, target.handle, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (withDepth)
      transitionImage(cmd, target.depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  });
  return target;
}

void destroyTarget(const Device& dev, Target& target) {
  if (target.depth != VK_NULL_HANDLE) {
    vkDestroyImageView(dev.device, target.depthView, nullptr);
    vkDestroyImage(dev.device, target.depth, nullptr);
    vkFreeMemory(dev.device, target.depthMemory, nullptr);
  }
  vkDestroyImageView(dev.device, target.view, nullptr);
  vkDestroyImage(dev.device, target.handle, nullptr);
  vkFreeMemory(dev.device, target.memory, nullptr);
  target = Target{};
}

std::vector<std::uint8_t> readTarget(const Device& dev, const Target& target) {
  VkDeviceSize bytes = static_cast<VkDeviceSize>(target.width) * target.height * 4;
  Buffer staging = createBuffer(dev, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  submitNow(dev, [&](VkCommandBuffer cmd) {
    transitionImage(cmd, target.handle, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { target.width, target.height, 1 };
    vkCmdCopyImageToBuffer(cmd, target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.handle, 1, &region);
    hostReadBarrier(cmd);
  });

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(bytes));
  std::memcpy(rgba.data(), staging.mapped, rgba.size());
  destroyBuffer(dev, staging);
  return rgba;
}
