#include "utils/Mesh.h"

#include <cmath>
#include <stdexcept>

static constexpr float PI = 3.14159265358979f;

static void quadIndices(std::vector<std::uint32_t>& indices, std::uint32_t tl, std::uint32_t bl,
                        std::uint32_t br, std::uint32_t tr)
{
  indices.insert(indices.end(), { tl, bl, br, tl, br, tr });
}

Mesh plane(float size, float uvScale, std::uint32_t segments) {
  if (segments == 0) throw std::runtime_error("plane needs at least one segment");
  Mesh mesh{};
  std::uint32_t n = segments + 1;

  for (std::uint32_t j = 0; j < n; ++j)
    for (std::uint32_t i = 0; i < n; ++i) {
      float s = static_cast<float>(i) / static_cast<float>(segments);
      float t = static_cast<float>(j) / static_cast<float>(segments);
      Vertex v{};
      v.pos     = glm::vec3((s - 0.5f) * size, 0.0f, (t - 0.5f) * size);
      v.normal  = glm::vec3(0.0f, 1.0f, 0.0f);
      v.uv      = glm::vec2(s, t) * uvScale;
      v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
      mesh.vertices.push_back(v);
    }

  for (std::uint32_t j = 0; j < segments; ++j)
    for (std::uint32_t i = 0; i < segments; ++i) {
      std::uint32_t tl = j * n + i;
      quadIndices(mesh.indices, tl, tl + n, tl + n + 1, tl + 1);
    }
  return mesh;
}

Mesh uvSphere(std::uint32_t rings, std::uint32_t segments, float radius) {
  if (rings < 2 || segments < 3) throw std::runtime_error("uvSphere needs rings >= 2, segments >= 3");
  Mesh mesh{};
  std::uint32_t row = segments + 1;

  for (std::uint32_t r = 0; r <= rings; ++r)
    for (std::uint32_t s = 0; s <= segments; ++s) {
      float u     = static_cast<float>(s) / static_cast<float>(segments);
      float v     = static_cast<float>(r) / static_cast<float>(rings);
      float theta = v * PI;
      float phi   = u * 2.0f * PI;

      glm::vec3 n(std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi));
      Vertex vert{};
      vert.pos     = n * radius;
      vert.normal  = n;
      vert.uv      = glm::vec2(u, v);
      vert.tangent = glm::vec4(std::cos(phi), 0.0f, -std::sin(phi), 1.0f);
      mesh.vertices.push_back(vert);
    }

  for (std::uint32_t r = 0; r < rings; ++r)
    for (std::uint32_t s = 0; s < segments; ++s) {
      std::uint32_t tl = r * row + s;
      quadIndices(mesh.indices, tl, tl + row, tl + row + 1, tl + 1);
    }
  return mesh;
}

Mesh atlasCube(const std::array<glm::vec4, 6>& islandRects, float size) {
  struct Face {
    glm::vec3 normal;
    glm::vec3 right;
    glm::vec3 up;
  };
  static const std::array<Face, 6> FACES = {{
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
    { {-1, 0, 0 }, { 0, 0,  1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 1, 0,  0 }, { 0, 0,-1 } },
    { { 0,-1, 0 }, { 1, 0,  0 }, { 0, 0, 1 } },
    { { 0, 0, 1 }, { 1, 0,  0 }, { 0, 1, 0 } },
    { { 0, 0,-1 }, {-1, 0,  0 }, { 0, 1, 0 } },
  }};

  Mesh  mesh{};
  float h = 0.5f * size;
  for (std::size_t f = 0; f < FACES.size(); ++f) {
    const Face&      face = FACES[f];
    const glm::vec4& rect = islandRects[f];
    std::uint32_t    base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 corners[4] = { { -1, 1 }, { -1, -1 }, { 1, -1 }, { 1, 1 } };
    for (const glm::vec2& c : corners) {
      Vertex v{};
      v.pos     = (face.normal + face.right * c.x + face.up * c.y) * h;
      v.normal  = face.normal;
      v.uv      = glm::vec2(c.x < 0.0f ? rect.x : rect.z, c.y > 0.0f ? rect.y : rect.w);
      v.tangent = glm::vec4(face.right, 1.0f);
      mesh.vertices.push_back(v);
    }
    quadIndices(mesh.indices, base, base + 1, base + 2, base + 3);
  }
  return mesh;
}

Mesh quad(float width, float height) {
  Mesh  mesh{};
  float hx = 0.5f * width;
  float hy = 0.5f * height;

  const glm::vec2 corners[4] = { { -1, 1 }, { -1, -1 }, { 1, -1 }, { 1, 1 } };
  for (const glm::vec2& c : corners) {
    Vertex v{};
    v.pos     = glm::vec3(c.x * hx, c.y * hy, 0.0f);
    v.normal  = glm::vec3(0.0f, 0.0f, 1.0f);
    v.uv      = glm::vec2(c.x < 0.0f ? 0.0f : 1.0f, c.y > 0.0f ? 0.0f : 1.0f);
    v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    mesh.vertices.push_back(v);
  }
  quadIndices(mesh.indices, 0, 1, 2, 3);
  return mesh;
}

Mesh transformed(const Mesh& mesh, const glm::mat4& m) {
  glm::mat3 linear(m);
  if (glm::determinant(linear) <= 0.0f)
    throw std::runtime_error("transformed() does not mirror meshes: det(m) must be > 0");
  glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));

  Mesh out = mesh;
  for (Vertex& v : out.vertices) {
    v.pos     = glm::vec3(m * glm::vec4(v.pos, 1.0f));
    v.normal  = glm::normalize(normalMatrix * v.normal);
    v.tangent = glm::vec4(glm::normalize(linear * glm::vec3(v.tangent)), v.tangent.w);
  }
  return out;
}

std::uint32_t append(Mesh& into, const Mesh& more) {
  std::uint32_t base  = static_cast<std::uint32_t>(into.vertices.size());
  std::uint32_t first = static_cast<std::uint32_t>(into.indices.size());
  into.vertices.insert(into.vertices.end(), more.vertices.begin(), more.vertices.end());
  for (std::uint32_t i : more.indices) into.indices.push_back(base + i);
  return first;
}
