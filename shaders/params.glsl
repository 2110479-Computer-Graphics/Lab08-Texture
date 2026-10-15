#ifndef PARAMS_GLSL_INCLUDED
#define PARAMS_GLSL_INCLUDED

layout(set = 0, binding = 0) uniform Params {
  vec2  resolution;
  float time;
  uint  mode;
  vec4  light;
  vec4  eye;
  uint  knob;
  float knobf;
  vec2  texSize;
  mat4  model;
  mat4  view;
  mat4  proj;
} u;

layout(set = 0, binding = 1) uniform sampler2D tex0;
layout(set = 0, binding = 2) uniform sampler2D tex1;
layout(set = 0, binding = 3) uniform sampler2D tex2;

#endif
