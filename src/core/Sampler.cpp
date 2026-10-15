#include "core/Sampler.h"

#include <algorithm>

static const char* const NAMES[FILTER_COUNT] = {
  "nearest", "bilinear", "trilinear", "aniso4", "aniso16", "nearest-mips"
};

const char* filterName(Filter filter) {
  return NAMES[static_cast<std::uint32_t>(filter)];
}

bool filterFromName(const std::string& name, Filter& out) {
  for (std::uint32_t i = 0; i < FILTER_COUNT; ++i)
    if (name == NAMES[i]) {
      out = static_cast<Filter>(i);
      return true;
    }
  return false;
}

static VkSampler createOne(const Device& dev, Filter filter, Wrap wrap, float& anisotropy) {
  VkSamplerAddressMode address =
    wrap == Wrap::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  VkSamplerCreateInfo samplerCI{};
  samplerCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.addressModeU = address;
  samplerCI.addressModeV = address;
  samplerCI.addressModeW = address;
  samplerCI.minLod       = 0.0f;
  samplerCI.maxLod       = VK_LOD_CLAMP_NONE;
  samplerCI.borderColor  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

  float wanted = 1.0f;
  switch (filter) {
    case Filter::Nearest:
      samplerCI.magFilter  = VK_FILTER_NEAREST;
      samplerCI.minFilter  = VK_FILTER_NEAREST;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      samplerCI.maxLod     = 0.0f;
      break;
    case Filter::Bilinear:
      samplerCI.magFilter  = VK_FILTER_LINEAR;
      samplerCI.minFilter  = VK_FILTER_LINEAR;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      samplerCI.maxLod     = 0.0f;
      break;
    case Filter::Trilinear:
    case Filter::Aniso4:
    case Filter::Aniso16:
      samplerCI.magFilter  = VK_FILTER_LINEAR;
      samplerCI.minFilter  = VK_FILTER_LINEAR;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      wanted = filter == Filter::Aniso4 ? 4.0f : filter == Filter::Aniso16 ? 16.0f : 1.0f;
      break;
    case Filter::NearestMips:
      samplerCI.magFilter  = VK_FILTER_NEAREST;
      samplerCI.minFilter  = VK_FILTER_NEAREST;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
  }

  anisotropy = 1.0f;
  if (wanted > 1.0f && dev.hasAnisotropy) {
    anisotropy                = std::min(wanted, dev.maxAnisotropy);
    samplerCI.anisotropyEnable = VK_TRUE;
    samplerCI.maxAnisotropy    = anisotropy;
  }

  VkSampler sampler = VK_NULL_HANDLE;
  check(vkCreateSampler(dev.device, &samplerCI, nullptr, &sampler), "vkCreateSampler");
  return sampler;
}

Samplers createSamplers(const Device& dev) {
  Samplers samplers{};
  for (std::uint32_t f = 0; f < FILTER_COUNT; ++f)
    for (std::uint32_t w = 0; w < 2; ++w)
      samplers.handles[f][w] =
        createOne(dev, static_cast<Filter>(f), static_cast<Wrap>(w), samplers.anisotropy[f]);
  return samplers;
}

void destroySamplers(const Device& dev, Samplers& samplers) {
  for (std::uint32_t f = 0; f < FILTER_COUNT; ++f)
    for (std::uint32_t w = 0; w < 2; ++w)
      vkDestroySampler(dev.device, samplers.handles[f][w], nullptr);
  samplers = Samplers{};
}
