#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(push_constant) uniform Push {
  vec4 color;
} push;

layout(location = 0) in vec3 inPos;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4  outColor;
layout(location = 1) out vec2  outUV;
layout(location = 2) out float outViewDepth;
layout(location = 3) flat out uint outSmoke;

void main() {
  vec4 viewPos = u.view * vec4(inPos, 1.0);
  gl_Position  = u.proj * viewPos;
  outColor     = push.color;
  outUV        = inUV;
  outSmoke     = 0u;
  outViewDepth = u.knob == 1u ? gl_Position.z / gl_Position.w : -viewPos.z;
}
