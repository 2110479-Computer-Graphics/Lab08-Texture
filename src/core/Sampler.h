#ifndef __SAMPLER_H_INCLUDED__
#define __SAMPLER_H_INCLUDED__

#include "core/Device.h"

#include <string>

enum class Filter : std::uint32_t { Nearest, Bilinear, Trilinear, Aniso4, Aniso16, NearestMips };
inline constexpr std::uint32_t FILTER_COUNT = 6;

inline constexpr Filter SWEEP_FILTERS[5] = {
  Filter::Nearest, Filter::Bilinear, Filter::Trilinear, Filter::Aniso4, Filter::Aniso16
};

enum class Wrap : std::uint32_t { Repeat, Clamp };

struct Samplers {
  VkSampler handles[FILTER_COUNT][2] = {};
  float     anisotropy[FILTER_COUNT] = {};

  VkSampler get(Filter filter, Wrap wrap) const {
    return handles[static_cast<std::uint32_t>(filter)][static_cast<std::uint32_t>(wrap)];
  }
};

Samplers createSamplers(const Device& dev);
void     destroySamplers(const Device& dev, Samplers& samplers);

const char* filterName(Filter filter);
bool        filterFromName(const std::string& name, Filter& out);

#endif
