#ifndef TRANSPARENT_GLSL_INCLUDED
#define TRANSPARENT_GLSL_INCLUDED

layout(location = 0) in vec4  inColor;
layout(location = 1) in vec2  inUV;
layout(location = 2) in float inViewDepth;
layout(location = 3) flat in uint inSmoke;

vec4 surface() {
  vec4 c = inColor;
  if (inSmoke == 1u) {
    vec2  d = inUV * 2.0 - 1.0;
    float s = max(1.0 - dot(d, d), 0.0);
    c.a *= s * s;
  } else {
    vec2 e = min(inUV, 1.0 - inUV);
    if (min(e.x, e.y) < 0.02) c = vec4(c.rgb * 0.15, 0.9);
  }
  return c;
}

#endif
