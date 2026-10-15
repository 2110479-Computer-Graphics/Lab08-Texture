#ifndef __TEXTURE_H_INCLUDED__
#define __TEXTURE_H_INCLUDED__

#include "core/Device.h"
#include "utils/image.h"

struct Texture {
  VkImage        image     = VK_NULL_HANDLE;
  VkDeviceMemory memory    = VK_NULL_HANDLE;
  VkImageView    view      = VK_NULL_HANDLE;
  VkImageView    srgbView  = VK_NULL_HANDLE;
  VkImageView    unormView = VK_NULL_HANDLE;
  VkFormat       format    = VK_FORMAT_UNDEFINED;
  std::uint32_t  width     = 0;
  std::uint32_t  height    = 0;
  std::uint32_t  levels    = 0;
  bool           renderTarget = false;
};

Texture createTexture(const Device& dev, const std::vector<Image>& levels,
                      VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
Texture createRenderTexture(const Device& dev, std::uint32_t width, std::uint32_t height,
                            VkFormat format);
void    destroyTexture(const Device& dev, Texture& texture);

void uploadTexture(const Device& dev, const Texture& texture, const std::vector<Image>& levels);

std::uint32_t levelWidth(const Texture& texture, std::uint32_t level);
std::uint32_t levelHeight(const Texture& texture, std::uint32_t level);

std::vector<std::uint8_t> readTextureBytes(const Device& dev, const Texture& texture,
                                           std::uint32_t level = 0);

std::vector<float> readTextureFloats(const Device& dev, const Texture& texture,
                                     std::uint32_t level = 0);

Image readTextureImage(const Device& dev, const Texture& texture, std::uint32_t level = 0);

std::uint32_t texelBytes(VkFormat format);
std::uint32_t channelCount(VkFormat format);
const char*   formatName(VkFormat format);

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height);

#endif
