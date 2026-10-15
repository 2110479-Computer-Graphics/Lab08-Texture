#ifndef FLOOR_HELPERS_GLSL_INCLUDED
#define FLOOR_HELPERS_GLSL_INCLUDED

vec3 floor_sample(vec2 uv) {
  if (u.knob <= 1u) return texture(tex0, uv).rgb;

  vec2 step = vec2(1.0, 0.5) / (16.0 * u.texSize);
  vec3 sum  = vec3(0.0);
  for (uint i = 0u; i < u.knob; ++i)
    sum += texture(tex0, uv + float(i) * step).rgb;
  return sum / float(u.knob);
}

vec3 mip_color(float lod) {
  const vec3 LEVELS[11] = vec3[11](
    vec3(0.85, 0.15, 0.15),
    vec3(0.95, 0.55, 0.10),
    vec3(0.95, 0.90, 0.20),
    vec3(0.30, 0.75, 0.20),
    vec3(0.10, 0.70, 0.70),
    vec3(0.20, 0.40, 0.95),
    vec3(0.55, 0.25, 0.85),
    vec3(0.90, 0.35, 0.75),
    vec3(0.95, 0.95, 0.95),
    vec3(0.55, 0.55, 0.55),
    vec3(0.20, 0.20, 0.20));
  float l = clamp(lod, 0.0, 10.0);
  int   i = int(floor(l));
  int   j = min(i + 1, 10);
  vec3  c = mix(LEVELS[i], LEVELS[j], l - float(i));

  float f    = fract(l);
  float band = 0.012 * pow(1.25, l);
  float line = l > band && l < 10.0 - band && min(f, 1.0 - f) < band ? 1.0 : 0.0;
  return mix(c, vec3(0.05), 0.85 * line);
}

vec3 heat(float t) {
  t = clamp(t, 0.0, 1.0);
  if (t <= 0.25)
    return mix(vec3(0.05, 0.08, 0.35), vec3(0.35, 0.80, 0.95), smoothstep(1.0 / 16.0, 0.25, t));
  float w = (t - 0.25) / 0.75;
  return w < 0.5 ? mix(vec3(1.00, 0.90, 0.25), vec3(0.95, 0.50, 0.10), w * 2.0)
                 : mix(vec3(0.95, 0.50, 0.10), vec3(0.75, 0.05, 0.10), w * 2.0 - 1.0);
}

#endif
