#ifndef __MESH_H_INCLUDED__
#define __MESH_H_INCLUDED__

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec4 tangent;
};

static_assert(sizeof(Vertex) == 48,            "Vertex must be 48 bytes, as meshLayout() says");
static_assert(offsetof(Vertex, pos)     ==  0, "pos must start at byte 0");
static_assert(offsetof(Vertex, normal)  == 12, "normal must start at byte 12");
static_assert(offsetof(Vertex, uv)      == 24, "uv must start at byte 24");
static_assert(offsetof(Vertex, tangent) == 32, "tangent must start at byte 32");

struct Mesh {
  std::vector<Vertex>        vertices;
  std::vector<std::uint32_t> indices;
};

Mesh plane(float size, float uvScale, std::uint32_t segments = 1);

Mesh uvSphere(std::uint32_t rings, std::uint32_t segments, float radius = 1.0f);

inline constexpr std::array<const char*, 6> CUBE_FACE_NAMES = {
  "+X", "-X", "+Y", "-Y", "+Z", "-Z"
};
Mesh atlasCube(const std::array<glm::vec4, 6>& islandRects, float size = 1.0f);

Mesh quad(float width = 1.0f, float height = 1.0f);

Mesh transformed(const Mesh& mesh, const glm::mat4& m);

std::uint32_t append(Mesh& into, const Mesh& more);

#endif
