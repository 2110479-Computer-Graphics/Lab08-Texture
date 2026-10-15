#ifndef __COLOR_H_INCLUDED__
#define __COLOR_H_INCLUDED__

#include <algorithm>
#include <cmath>
#include <cstdint>

inline double srgbToLinear(double c) {
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double linearToSrgb(double c) {
  return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

inline double luminance(double r, double g, double b) {
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

inline int toByte(double v) {
  return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

inline double byteToLinear(std::uint8_t b) { return srgbToLinear(b / 255.0); }

#endif
