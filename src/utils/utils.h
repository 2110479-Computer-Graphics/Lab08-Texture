#ifndef __UTILS_H_INCLUDED__
#define __UTILS_H_INCLUDED__

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

inline constexpr std::uint32_t WIDTH  = 800;
inline constexpr std::uint32_t HEIGHT = 600;

inline constexpr std::uint32_t WARMUP_FRAMES   = 5;
inline constexpr std::uint32_t MEASURED_FRAMES = 21;

inline constexpr std::uint32_t SWEEP_WARMUP    = 20;

inline constexpr std::uint32_t FRAME_REPEATS   = 20;

inline constexpr std::uint32_t SWEEP_REPEATS   = 200;

#ifdef NDEBUG
inline constexpr bool ENABLE_VALIDATION = false;
#else
inline constexpr bool ENABLE_VALIDATION = true;

#endif

inline const std::vector<const char*> VALIDATION_LAYERS = {
  "VK_LAYER_KHRONOS_validation"
};

void check(VkResult result, const char* what);

std::string label(const std::string& name, std::size_t width = 15);

std::string indent();

std::vector<char> readFile(const std::string& path);

#endif
