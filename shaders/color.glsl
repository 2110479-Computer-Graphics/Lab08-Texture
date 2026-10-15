#ifndef COLOR_GLSL_INCLUDED
#define COLOR_GLSL_INCLUDED

// TODO(TASK 1a)
// sRGB-encoded value (what the bytes of a colour texture mean) -> linear light.
// The exact piecewise curve, not pow(c, 2.2).
vec3 srgb_to_linear(vec3 c) {
  return c;
}

// TODO(TASK 1b)
// Linear light -> sRGB-encoded value (what the UNORM target and the screen expect).
// The exact inverse of srgb_to_linear.
vec3 linear_to_srgb(vec3 c) {
  return c;
}

// TODO(TASK 1c)
// A debug view for albedo textures: luminance of the LINEAR colour, too dark
// (< 0.04) shows blue, too bright (> 0.9) shows red, anything else shows that
// luminance as a grey, encoded for display.
vec3 albedo_check(vec3 lin) {
  return lin;
}

#endif
