#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <color.glsl>
#include <floor_helpers.glsl>

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

// TODO(TASK 3a)
// The level of detail: log2 of rho, where rho is the LONGER of the two footprint
// sides (the length of dFdx and of dFdy of the texel position uv * u.texSize).
// Level 0 is one texel a pixel, level 1 two, level 2 four, ...
float lod_of(vec2 uv) {
  return 0.0;
}

// TODO(TASK 3b)
// The anisotropy ratio: the longer footprint side over the shorter one, clamped to
// [1, 16] (16 is the most taps an anisotropic sampler takes). 1 means a square
// footprint; 8 means eight texels along the long side for every one across.
float aniso_ratio(vec2 uv) {
  return 1.0;
}

void main() {
  float lod   = lod_of(inUV);
  float ratio = aniso_ratio(inUV);

  if (u.mode == 3u) {
    outColor = vec4(lod / 16.0, ratio / 16.0, 0.0, 1.0);
    return;
  }

  vec3 col;
  if      (u.mode == 0u) col = linear_to_srgb(floor_sample(inUV));
  else if (u.mode == 1u) col = mip_color(lod);
  else                   col = heat(ratio / 16.0);
  outColor = vec4(col, 1.0);
}
