#ifndef HASH_GLSL_INCLUDED
#define HASH_GLSL_INCLUDED

uint pcg(uint v) {
  v = v * 747796405u + 2891336453u;
  uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
  return (w >> 22u) ^ w;
}

uint pcg2(uvec2 p) { return pcg(p.x ^ pcg(p.y)); }
uint pcg3(uvec3 p) { return pcg(p.x ^ pcg(p.y ^ pcg(p.z))); }

float hash01(uint v)   { return float(pcg(v) >> 8u) * (1.0 / 16777216.0); }
float hash01(uvec2 p)  { return float(pcg2(p) >> 8u) * (1.0 / 16777216.0); }
float hash01(uvec3 p)  { return float(pcg3(p) >> 8u) * (1.0 / 16777216.0); }

#endif
