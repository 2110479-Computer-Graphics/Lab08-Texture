#ifndef OIT_HEAT_GLSL_INCLUDED
#define OIT_HEAT_GLSL_INCLUDED

vec3 heat(float t) {
  t = clamp(t, 0.0, 1.0);
  vec3 c = clamp(vec3(4.0 * t - 2.0, 2.0 - abs(4.0 * t - 2.0), 2.0 - 4.0 * t), 0.0, 1.0);
  return c;
}

#endif
