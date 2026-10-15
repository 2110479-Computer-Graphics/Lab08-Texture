#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main() {
  vec2 lod = textureQueryLod(tex0, inUV);
  outColor = vec4(lod.y, lod.x, 0.0, 1.0);
}
