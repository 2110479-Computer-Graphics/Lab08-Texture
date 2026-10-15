#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <color.glsl>

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorld;

layout(location = 0) out vec4 outColor;

const vec3  WARM      = vec3(1.0, 0.62, 0.32) * 1.0;
const vec3  COOL      = vec3(0.32, 0.58, 1.0) * 1.2;
const float THROW     = 6.0;
const float COS_INNER = cos(radians(4.5));
const float COS_OUTER = cos(radians(10.2));

float spot(vec2 aim, vec3 p) {
  vec3  d    = p - vec3(aim, THROW);
  float dist = length(d);
  float cone = smoothstep(COS_OUTER, COS_INNER, -d.z / dist);
  return cone * THROW * THROW / (dist * dist);
}

void main() {
  vec3 raw = texture(tex1, inUV).rgb;
  if (u.knob == 1u) {
    outColor = vec4(raw, 1.0);
    return;
  }

  float a     = spot(u.light.xy, inWorld);
  float b     = spot(u.light.zw, inWorld);
  vec3  light = WARM * a + COOL * b;
  float split = u.knobf;

  vec3 col;
  if (gl_FragCoord.x < split)
    col = linear_to_srgb(srgb_to_linear(raw) * light);
  else if (u.mode == 0u)
    col = raw * (linear_to_srgb(WARM) * a + linear_to_srgb(COOL) * b);
  else if (u.mode == 1u)
    col = albedo_check(srgb_to_linear(raw));
  else if (u.mode == 2u)
    col = linear_to_srgb(raw * light);
  else
    col = srgb_to_linear(raw) * light;

  if (split > 0.0 && split < u.resolution.x && abs(gl_FragCoord.x - split) < 1.0) col = vec3(0.9);

  outColor = vec4(col, 1.0);
}
