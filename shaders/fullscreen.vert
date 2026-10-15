#version 450

const vec2 CORNERS[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));

layout(location = 0) out vec2 outUV;

void main() {
  vec2 corner = CORNERS[gl_VertexIndex];
  outUV       = corner * 0.5 + 0.5;
  gl_Position = vec4(corner, 0.0, 1.0);
}
