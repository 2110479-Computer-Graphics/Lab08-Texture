#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <hash.glsl>

layout(location = 0) in vec3 inPos;
layout(location = 2) in vec2 inUV;
layout(location = 4) in uint inId;

layout(location = 0) out vec4  outColor;
layout(location = 1) out vec2  outUV;
layout(location = 2) out float outViewDepth;
layout(location = 3) flat out uint outSmoke;

const float CLOUD_NEAR = 3.0;
const float CLOUD_FAR  = 30.0;
const float TAN_X      = 0.55228475;
const float TAN_Y      = 0.41421356;

float h(uint k) { return hash01(uvec2(inId, k)); }

void main() {
  float d    = CLOUD_NEAR + (CLOUD_FAR - CLOUD_NEAR) * max(h(0u), h(1u));
  float x    = (2.0 * h(3u) - 1.0) * 1.1 * TAN_X * d;
  float y    = (1.3 * h(4u) - 0.3) * TAN_Y * d;
  float size = 0.4 + 0.6 * h(5u) + 0.06 * d;

  bool  dense = h(6u) < 0.04;
  float grey  = dense ? 0.05 : 0.35 + 0.4 * h(7u);
  float alpha = dense ? 0.9 : 0.10 + 0.25 * h(8u);

  vec4 centre  = u.view * u.model * vec4(x, y, -d, 1.0);
  vec4 viewPos = centre + vec4(inPos.xy * size, 0.0, 0.0);
  gl_Position  = u.proj * viewPos;
  outColor     = vec4(grey * vec3(1.0, 0.97, 0.93), alpha);
  outUV        = inUV;
  outSmoke     = 1u;
  outViewDepth = u.knob == 1u ? gl_Position.z / gl_Position.w
                              : -centre.z;
}
