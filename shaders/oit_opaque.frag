#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(location = 0) in vec3 inWorld;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;

float checker(vec2 p) {
  vec2 f = floor(p);
  return mod(f.x + f.y, 2.0);
}

void main() {
  vec3  n       = normalize(inNormal);
  bool  isFloor = abs(n.y) > 0.5;
  float k       = isFloor ? checker(inWorld.xz) : checker(inWorld.xy * 0.5);
  vec3  albedo  = isFloor ? mix(vec3(0.20, 0.17, 0.13), vec3(0.42, 0.37, 0.30), k)
                          : mix(vec3(0.28, 0.29, 0.31), vec3(0.62, 0.63, 0.66), k);

  vec3 lit = albedo * (0.35 + 0.65 * u.light.w * max(dot(n, u.light.xyz), 0.0));

  if (u.mode == 3u) lit = vec3(dot(lit, vec3(0.2126, 0.7152, 0.0722)) * 0.35);
  outColor = vec4(lit, 1.0);
}
