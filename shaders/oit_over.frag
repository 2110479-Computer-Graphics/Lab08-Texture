#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <transparent.glsl>

layout(location = 0) out vec4 outColor;

void main() {
  outColor = surface();
}
