#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outWorld;
layout(location = 1) out vec3 outNormal;

void main() {
  outWorld    = inPos;
  outNormal   = inNormal;
  gl_Position = u.proj * u.view * vec4(inPos, 1.0);
}
