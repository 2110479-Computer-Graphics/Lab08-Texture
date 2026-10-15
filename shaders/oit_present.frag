#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <color.glsl>
#include <oit_heat.glsl>

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

const float HALF_MAX = 65504.0;

void main() {
  ivec2 p      = ivec2(gl_FragCoord.xy);
  vec3  scene  = texelFetch(tex0, p, 0).rgb;
  vec4  accum  = texelFetch(tex1, p, 0);
  float reveal = texelFetch(tex2, p, 0).r;

  bool saturated = false;
  for (int i = 0; i < 4; ++i)
    saturated = saturated || isinf(accum[i]) || isnan(accum[i]) || accum[i] >= HALF_MAX;

  vec3 col;
  if      (u.mode == 0u) col = linear_to_srgb(clamp(scene, 0.0, 1.0));
  else if (u.mode == 1u) col = saturated ? vec3(1.0, 0.0, 1.0) : vec3(reveal);
  else if (u.mode == 2u) col = accum.a > 0.0 ? heat(log2(max(accum.a, 1.0)) / log2(HALF_MAX))
                                             : vec3(0.0);
  else                   col = scene;
  outColor = vec4(col, 1.0);
}
