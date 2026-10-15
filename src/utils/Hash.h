#ifndef __HASH_H_INCLUDED__
#define __HASH_H_INCLUDED__

#include <glm/glm.hpp>

#include <cstdint>

inline std::uint32_t pcg(std::uint32_t v) {
  v = v * 747796405u + 2891336453u;
  std::uint32_t w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
  return (w >> 22u) ^ w;
}

inline std::uint32_t pcg(glm::uvec2 p) { return pcg(p.x ^ pcg(p.y)); }
inline std::uint32_t pcg(glm::uvec3 p) { return pcg(p.x ^ pcg(p.y ^ pcg(p.z))); }

inline float hash01(std::uint32_t v) { return static_cast<float>(pcg(v) >> 8) * (1.0f / 16777216.0f); }
inline float hash01(glm::uvec2 p)    { return static_cast<float>(pcg(p) >> 8) * (1.0f / 16777216.0f); }
inline float hash01(glm::uvec3 p)    { return static_cast<float>(pcg(p) >> 8) * (1.0f / 16777216.0f); }

#endif
