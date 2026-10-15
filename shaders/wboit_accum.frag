#version 450
#extension GL_GOOGLE_include_directive : require

#include <params.glsl>
#include <transparent.glsl>

layout(location = 0) out vec4 outAccum;
layout(location = 1) out vec4 outReveal;

void main() {
  vec4 c = surface();

  // TODO(TASK 4a)
  // c is this fragment's straight colour c.rgb (linear) and alpha c.a.
  // The weight: w = clamp(0.03 / (1e-5 + (z / 200)^4), 1e-2, 3e3), with z the
  // VIEW-SPACE depth in metres (inViewDepth), not the window depth gl_FragCoord.z.
  // accum: the colour premultiplied by alpha, times w, and alpha times w.
  // reveal: the alpha, so the blend leaves the product of (1 - alpha).
  // Until then nothing is added and nothing is covered: WBOIT shows the opaque scene.
  outAccum  = vec4(0.0);
  outReveal = vec4(0.0);
}
