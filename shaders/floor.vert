#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(location = 0) in vec3 inPos;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec2 outUV;

void main() {
  outUV       = inUV;
  gl_Position = u.proj * u.view * u.model * vec4(inPos, 1.0);
}
