#include "core/Texture.h"

#include "core/Buffer.h"
#include "core/Target.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

static bool isRgba8(VkFormat format) {
  return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
}

std::uint32_t texelBytes(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:       return 4;
    case VK_FORMAT_R8_UNORM:            return 1;
    case VK_FORMAT_R16_SFLOAT:          return 2;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    case VK_FORMAT_R32_SFLOAT:          return 4;
    case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
    default: throw std::runtime_error("texture format not supported by this lab's core");
  }
}

std::uint32_t channelCount(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT: return 1;
    default:                   return 4;
  }
}

const char* formatName(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB:       return "R8G8B8A8_SRGB";
    case VK_FORMAT_R8_UNORM:            return "R8_UNORM";
    case VK_FORMAT_R16_SFLOAT:          return "R16_SFLOAT";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
    case VK_FORMAT_R32_SFLOAT:          return "R32_SFLOAT";
    case VK_FORMAT_R32G32B32A32_SFLOAT: return "R32G32B32A32_SFLOAT";
    case VK_FORMAT_D32_SFLOAT:          return "D32_SFLOAT";
    default:                            return "(another format)";
  }
}

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height) {
  std::uint32_t levels = 1;
  for (std::uint32_t size = std::max(width, height); size > 1; size /= 2) ++levels;
  return levels;
}

std::uint32_t levelWidth(const Texture& texture, std::uint32_t level) {
  return std::max(texture.width >> level, 1u);
}

std::uint32_t levelHeight(const Texture& texture, std::uint32_t level) {
  return std::max(texture.height >> level, 1u);
}

static void createViews(const Device& dev, Texture& texture) {
  if (!isRgba8(texture.format)) return;
  VkFormat other = texture.format == VK_FORMAT_R8G8B8A8_SRGB ? VK_FORMAT_R8G8B8A8_UNORM
                                                             : VK_FORMAT_R8G8B8A8_SRGB;
  VkImageView second = createView(dev, texture.image, other, VK_IMAGE_ASPECT_COLOR_BIT,
                                  texture.levels);
  texture.srgbView  = texture.format == VK_FORMAT_R8G8B8A8_SRGB ? texture.view : second;
  texture.unormView = texture.format == VK_FORMAT_R8G8B8A8_SRGB ? second : texture.view;
}

static void checkLevels(const std::vector<Image>& levels, std::uint32_t width, std::uint32_t height,
                        std::uint32_t maxLevels)
{
  if (levels.empty()) throw std::runtime_error("a texture needs at least one level");
  if (levels.size() > maxLevels) throw std::runtime_error("more levels than the texture has");
  for (std::size_t i = 0; i < levels.size(); ++i) {
    std::uint32_t w = std::max(width >> i, 1u);
    std::uint32_t h = std::max(height >> i, 1u);
    if (levels[i].width != w || levels[i].height != h || levels[i].rgba.size() != std::size_t(w) * h * 4)
      throw std::runtime_error("mip level " + std::to_string(i) + " should be " + std::to_string(w) +
                               "x" + std::to_string(h) + " RGBA8");
  }
}

static void copyLevels(const Device& dev, const Texture& texture, const std::vector<Image>& levels,
                       VkImageLayout from)
{
  VkDeviceSize total = 0;
  for (const Image& level : levels) total += level.rgba.size();
  Buffer staging = createBuffer(dev, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

  std::vector<VkBufferImageCopy> regions;
  VkDeviceSize                   offset = 0;
  for (std::size_t i = 0; i < levels.size(); ++i) {
    std::memcpy(static_cast<std::uint8_t*>(staging.mapped) + offset, levels[i].rgba.data(),
                levels[i].rgba.size());
    VkBufferImageCopy region{};
    region.bufferOffset     = offset;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(i), 0, 1 };
    region.imageExtent      = { levels[i].width, levels[i].height, 1 };
    regions.push_back(region);
    offset += levels[i].rgba.size();
  }

  std::uint32_t count = static_cast<std::uint32_t>(levels.size());
  submitNow(dev, [&](VkCommandBuffer cmd) {
    transitionImage(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, from,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, count);
    vkCmdCopyBufferToImage(cmd, staging.handle, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           count, regions.data());
    transitionImage(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    0, count);
  });
  destroyBuffer(dev, staging);
}

Texture createTexture(const Device& dev, const std::vector<Image>& levels, VkFormat format) {
  if (!isRgba8(format))
    throw std::runtime_error("createTexture takes R8G8B8A8_SRGB or R8G8B8A8_UNORM");
  if (levels.empty()) throw std::runtime_error("a texture needs at least one level");

  Texture texture{};
  texture.format = format;
  texture.width  = levels[0].width;
  texture.height = levels[0].height;
  texture.levels = static_cast<std::uint32_t>(levels.size());
  checkLevels(levels, texture.width, texture.height, fullMipCount(texture.width, texture.height));

  createImage(dev, texture.width, texture.height, texture.levels, format,
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT, texture.image,
              texture.memory, texture.view);
  createViews(dev, texture);
  copyLevels(dev, texture, levels, VK_IMAGE_LAYOUT_UNDEFINED);
  return texture;
}

Texture createRenderTexture(const Device& dev, std::uint32_t width, std::uint32_t height,
                            VkFormat format)
{
  texelBytes(format);

  VkFormatProperties support{};
  vkGetPhysicalDeviceFormatProperties(dev.gpu, format, &support);
  if (!(support.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
    throw std::runtime_error(std::string("this GPU cannot render to ") + formatName(format));

  Texture texture{};
  texture.format       = format;
  texture.width        = width;
  texture.height       = height;
  texture.levels       = 1;
  texture.renderTarget = true;

  createImage(dev, width, height, 1, format,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT, isRgba8(format) ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0,
              texture.image, texture.memory, texture.view);
  createViews(dev, texture);

  submitNow(dev, [&](VkCommandBuffer cmd) {
    transitionImage(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue       zero{};
    VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    transitionImage(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  });
  return texture;
}

void destroyTexture(const Device& dev, Texture& texture) {
  if (texture.image == VK_NULL_HANDLE) return;
  if (texture.srgbView != VK_NULL_HANDLE && texture.srgbView != texture.view)
    vkDestroyImageView(dev.device, texture.srgbView, nullptr);
  if (texture.unormView != VK_NULL_HANDLE && texture.unormView != texture.view)
    vkDestroyImageView(dev.device, texture.unormView, nullptr);
  vkDestroyImageView(dev.device, texture.view, nullptr);
  vkDestroyImage(dev.device, texture.image, nullptr);
  vkFreeMemory(dev.device, texture.memory, nullptr);
  texture = Texture{};
}

void uploadTexture(const Device& dev, const Texture& texture, const std::vector<Image>& levels) {
  if (!isRgba8(texture.format)) throw std::runtime_error("uploadTexture takes RGBA8 textures only");
  checkLevels(levels, texture.width, texture.height, texture.levels);
  copyLevels(dev, texture, levels, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

std::vector<std::uint8_t> readTextureBytes(const Device& dev, const Texture& texture,
                                           std::uint32_t level)
{
  if (level >= texture.levels) throw std::runtime_error("readTexture: no such mip level");
  std::uint32_t w     = levelWidth(texture, level);
  std::uint32_t h     = levelHeight(texture, level);
  VkDeviceSize  bytes = static_cast<VkDeviceSize>(w) * h * texelBytes(texture.format);
  Buffer staging = createBuffer(dev, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  submitNow(dev, [&](VkCommandBuffer cmd) {
    transitionImage(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    level, 1);
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
    region.imageExtent      = { w, h, 1 };
    vkCmdCopyImageToBuffer(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.handle, 1, &region);
    transitionImage(cmd, texture.image, VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    level, 1);
    hostReadBarrier(cmd);
  });

  std::vector<std::uint8_t> out(static_cast<std::size_t>(bytes));
  std::memcpy(out.data(), staging.mapped, out.size());
  destroyBuffer(dev, staging);
  return out;
}

static float halfToFloat(std::uint16_t h) {
  std::uint32_t sign     = (h >> 15) & 0x1u;
  std::uint32_t exponent = (h >> 10) & 0x1fu;
  std::uint32_t mantissa = h & 0x3ffu;

  float value = 0.0f;
  if (exponent == 0)
    value = std::ldexp(static_cast<float>(mantissa), -24);
  else if (exponent == 31)
    value = mantissa != 0 ? std::numeric_limits<float>::quiet_NaN()
                          : std::numeric_limits<float>::infinity();
  else
    value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
  return sign != 0 ? -value : value;
}

std::vector<float> readTextureFloats(const Device& dev, const Texture& texture, std::uint32_t level) {
  std::vector<std::uint8_t> raw = readTextureBytes(dev, texture, level);
  std::vector<float>        out;

  switch (texture.format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_R8_UNORM:
      out.reserve(raw.size());
      for (std::uint8_t b : raw) out.push_back(static_cast<float>(b) / 255.0f);
      break;
    case VK_FORMAT_R16_SFLOAT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      out.reserve(raw.size() / 2);
      for (std::size_t i = 0; i + 1 < raw.size(); i += 2) {
        std::uint16_t h = 0;
        std::memcpy(&h, &raw[i], 2);
        out.push_back(halfToFloat(h));
      }
      break;
    default:
      out.resize(raw.size() / 4);
      std::memcpy(out.data(), raw.data(), raw.size());
      break;
  }
  return out;
}

Image readTextureImage(const Device& dev, const Texture& texture, std::uint32_t level) {
  if (!isRgba8(texture.format)) throw std::runtime_error("readTextureImage takes RGBA8 textures only");
  Image image(levelWidth(texture, level), levelHeight(texture, level));
  image.rgba = readTextureBytes(dev, texture, level);
  return image;
}
