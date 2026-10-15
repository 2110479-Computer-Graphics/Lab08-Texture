#ifndef __PARAMS_H_INCLUDED__
#define __PARAMS_H_INCLUDED__

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

struct Params {
  glm::vec2     resolution{0.0f};
  float         time  = 1.0f;
  std::uint32_t mode  = 0;
  glm::vec4     light{0.0f, 1.0f, 0.0f, 1.0f};
  glm::vec4     eye{0.0f, 0.0f, 3.0f, 1.0f};
  std::uint32_t knob  = 0;
  float         knobf = 0.0f;
  glm::vec2     texSize{1.0f};
  glm::mat4     model{1.0f};
  glm::mat4     view{1.0f};
  glm::mat4     proj{1.0f};
};

static_assert(sizeof(Params) == 256,               "Params must match the std140 block in params.glsl");
static_assert(offsetof(Params, resolution) ==   0, "resolution must start at byte 0");
static_assert(offsetof(Params, time)       ==   8, "time must start at byte 8");
static_assert(offsetof(Params, mode)       ==  12, "mode must start at byte 12");
static_assert(offsetof(Params, light)      ==  16, "light must start at byte 16");
static_assert(offsetof(Params, eye)        ==  32, "eye must start at byte 32");
static_assert(offsetof(Params, knob)       ==  48, "knob must start at byte 48");
static_assert(offsetof(Params, knobf)      ==  52, "knobf must start at byte 52");
static_assert(offsetof(Params, texSize)    ==  56, "texSize must start at byte 56");
static_assert(offsetof(Params, model)      ==  64, "model must start at byte 64");
static_assert(offsetof(Params, view)       == 128, "view must start at byte 128");
static_assert(offsetof(Params, proj)       == 192, "proj must start at byte 192");

#endif
