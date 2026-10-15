#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <color.glsl>

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

const float TILE   = 256.0;
const float TILE_Y = 172.0;
const float TILE_X[3] = float[3](8.0, 272.0, 536.0);

void main() {
  vec2 p    = gl_FragCoord.xy;
  int  tile = -1;
  for (int i = 0; i < 3; ++i)
    if (p.x >= TILE_X[i] && p.x < TILE_X[i] + TILE && p.y >= TILE_Y && p.y < TILE_Y + TILE)
      tile = i;

  vec2 corner = vec2(TILE_X[max(tile, 0)], TILE_Y);
  vec2 uv     = clamp((p - corner) / TILE, 0.0, 1.0);

  vec3 hardware = texture(tex0, uv).rgb;
  vec3 raw      = texture(tex1, uv).rgb;
  vec3 yours    = srgb_to_linear(raw);

  if (tile < 0) {
    outColor = vec4(vec3(0.1), 1.0);
    return;
  }

  vec3 lin = tile == 0 ? hardware : tile == 1 ? yours : raw;

  vec3 col;
  if      (u.mode == 0u) col = linear_to_srgb(lin);
  else if (u.mode == 1u) col = albedo_check(lin);
  else if (u.mode == 2u) col = lin;
  else                   col = tile == 1 ? abs(hardware - yours) * 64.0 : vec3(0.0);

  outColor = vec4(col, 1.0);
}
