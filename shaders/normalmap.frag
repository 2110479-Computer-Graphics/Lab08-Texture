#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <color.glsl>

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 Ng = normalize(inNormal);
  vec3 T  = normalize(inTangent);
  vec3 B  = normalize(inBitangent);
  vec3 L  = normalize(u.light.xyz);

  // TODO(TASK 2b)
  // Read the normal map (tex1) at inUV and decode it to a vector n in [-1, 1]. A DirectX
  // map (u.knob == 1) stores green the other way up: flip n.y. Then take n from
  // tangent space to world space with the matrix whose columns are T, B and Ng,
  // and normalize the result.
  // For now N is the geometric normal: the map is ignored, the sphere is smooth.
  vec3 N = Ng;

  vec3 albedo = texture(tex0, inUV).rgb;
  vec3 lit    = albedo * (0.08 + 0.92 * max(dot(N, L), 0.0)) * u.light.w;

  vec3 col;
  if      (u.mode == 0u) col = linear_to_srgb(lit);
  else if (u.mode == 1u) col = N * 0.5 + 0.5;
  else if (u.mode == 2u) col = texture(tex1, inUV).xyz;
  else                   col = vec3(length(textureLod(tex1, inUV, u.knobf).xyz * 2.0 - 1.0));

  outColor = vec4(col, 1.0);
}
