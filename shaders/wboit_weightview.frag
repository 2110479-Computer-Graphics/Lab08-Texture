#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <transparent.glsl>
#include <oit_heat.glsl>

layout(location = 0) out vec4 outColor;

void main() {
  vec4 c = surface();
  if (c.a < 0.1) discard;

  float z = u.knob == 1u ? gl_FragCoord.z : inViewDepth;
  float w = clamp(0.03 / (1e-5 + pow(z / 200.0, 4.0)), 1e-2, 3e3);
  float t = log2(w / 30.0) / log2(100.0);
  outColor = vec4(heat(t), 1.0);
}
