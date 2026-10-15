#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(location = 0) in vec3 inPos;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorld;

void main() {
  vec4 world  = u.model * vec4(inPos, 1.0);
  outUV       = inUV;
  outWorld    = world.xyz;
  gl_Position = u.proj * u.view * world;
}
