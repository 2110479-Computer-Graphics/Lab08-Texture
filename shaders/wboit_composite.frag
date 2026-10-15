#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);

  // TODO(TASK 4b)
  // Read accum (tex0) and reveal (tex1, .r) at p with texelFetch.
  // Where nothing transparent was drawn (reveal 1), leave the scene alone. Elsewhere
  // output the weighted average colour accum.rgb / accum.a (keep the division away
  // from 0) with alpha 1 - reveal.
  // Until then alpha 0: the scene stays as it is.
  outColor = vec4(0.0);
}
