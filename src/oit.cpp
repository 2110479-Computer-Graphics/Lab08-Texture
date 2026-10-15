#include "core/Frame.h"
#include "core/Geometry.h"
#include "core/Sampler.h"
#include "core/Shader.h"
#include "core/Timer.h"
#include "utils/Hash.h"
#include "utils/Mesh.h"
#include "utils/image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static const glm::vec3 EYE(1.2f, 5.0f, 7.5f);
static const glm::vec3 LOOK_AT(0.0f, 1.4f, 0.0f);
static constexpr float FOV_Y  = 0.78539816f;
static constexpr float Z_NEAR = 0.1f;
static constexpr float Z_FAR  = 100.0f;

static constexpr float WALL_Z = -24.0f;

static constexpr float CLOUD_NEAR = 3.0f;
static constexpr float CLOUD_FAR  = 30.0f;
static constexpr float TAN_X      = 0.55228475f;
static constexpr float TAN_Y      = 0.41421356f;

static constexpr std::uint32_t DEFAULT_PARTICLES = 1000;
static constexpr std::uint32_t MANY_PARTICLES    = 10000;

static constexpr float PANE_W     = 4.0f;
static constexpr float PANE_H     = 3.0f;
static constexpr float PANE_ALPHA = 0.45f;

struct PaneSpec {
  const char* name;
  glm::vec3   centre;
  float       yawDeg;
  float       tiltDeg;
  glm::vec3   color;
};

static const std::array<PaneSpec, 3> PANES = { {
  { "red",   { 0.0f, 1.5f,  0.15f },  45.0f,   0.0f, { 0.85f, 0.05f, 0.04f } },
  { "green", { 0.0f, 1.5f, -0.15f }, -45.0f,   0.0f, { 0.05f, 0.62f, 0.10f } },
  { "blue",  { 0.0f, 1.4f,  0.0f  },   0.0f, -60.0f, { 0.04f, 0.14f, 0.90f } },
} };

static glm::mat4 paneMatrix(const PaneSpec& p) {
  return glm::translate(glm::mat4(1.0f), p.centre) *
         glm::rotate(glm::mat4(1.0f), glm::radians(p.yawDeg), glm::vec3(0.0f, 1.0f, 0.0f)) *
         glm::rotate(glm::mat4(1.0f), glm::radians(p.tiltDeg), glm::vec3(1.0f, 0.0f, 0.0f));
}

static constexpr float     LAYER_UP     = 0.6f;
static const     glm::uvec2 LAYER_PIXEL(WIDTH / 2, 120);
static constexpr float     NEAR_LAYER_M = 4.0f;
static constexpr float     FAR_LAYER_M  = 20.0f;
static const     glm::vec4 NEAR_LAYER(1.0f, 0.0f, 0.0f, 0.30f);
static const     glm::vec4 FAR_LAYER(0.0f, 0.0f, 1.0f, 0.60f);

static constexpr float HALF_MAX = 65504.0f;

enum class Method : std::uint32_t { Unsorted = 0, Sorted = 1, Wboit = 2 };
static const std::array<const char*, 3> METHOD_NAMES = { "unsorted", "sorted", "wboit" };

static Method methodFromName(const std::string& name) {
  if (name.empty() || name == "wboit") return Method::Wboit;
  if (name == "unsorted") return Method::Unsorted;
  if (name == "sorted") return Method::Sorted;
  throw std::runtime_error("--method wants unsorted, sorted or wboit, not " + name);
}

static const char* methodName(Method m) { return METHOD_NAMES[static_cast<std::uint32_t>(m)]; }

static glm::vec3 particleCentre(std::uint32_t id) {
  auto  h = [id](std::uint32_t k) { return hash01(glm::uvec2(id, k)); };
  float d = CLOUD_NEAR + (CLOUD_FAR - CLOUD_NEAR) * std::max(h(0), h(1));
  return glm::vec3((2.0f * h(3) - 1.0f) * 1.1f * TAN_X * d, (1.3f * h(4) - 0.3f) * TAN_Y * d, -d);
}

static double weightOf(double z) {
  return std::clamp(0.03 / (1e-5 + std::pow(z / 200.0, 4.0)), 1e-2, 3e3);
}

struct PaneSet {
  GpuMesh                mesh;
  std::vector<glm::vec4> colors;
  std::vector<glm::vec3> centres;
};

struct Draw {
  bool          pane  = false;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};

struct DrawList {
  const PaneSet*    panes     = nullptr;
  const Buffer*     instances = nullptr;
  std::vector<Draw> draws;
};

static DrawList inOrder(const PaneSet& panes, const Buffer& ordered, std::uint32_t particles) {
  DrawList list{ &panes, &ordered, {} };
  for (std::uint32_t i = 0; i < panes.colors.size(); ++i) list.draws.push_back({ true, i, 0 });
  if (particles > 0) list.draws.push_back({ false, 0, particles });
  return list;
}

static DrawList inReverse(const PaneSet& panes, const Buffer& reversed, std::uint32_t particles) {
  DrawList list{ &panes, &reversed, {} };
  if (particles > 0) list.draws.push_back({ false, 0, particles });
  for (std::uint32_t i = static_cast<std::uint32_t>(panes.colors.size()); i-- > 0;)
    list.draws.push_back({ true, i, 0 });
  return list;
}

static void fillReversed(const Buffer& reversed, std::uint32_t particles) {
  std::vector<std::uint32_t> ids(particles);
  for (std::uint32_t i = 0; i < particles; ++i) ids[i] = particles - 1 - i;
  if (particles > 0) uploadBuffer(reversed, ids.data(), sizeof(std::uint32_t) * ids.size());
}

struct Sorter {
  std::vector<glm::vec3>     paneCentres;
  std::vector<glm::vec3>     particleCentres;
  std::vector<float>         depth;
  std::vector<std::uint32_t> order;
  std::vector<std::uint32_t> ids;
  std::vector<Draw>          draws;

  void sort(const glm::mat4& view, std::uint32_t particles) {
    std::uint32_t panes   = static_cast<std::uint32_t>(paneCentres.size());
    std::uint32_t objects = panes + particles;
    glm::vec4     row(view[0][2], view[1][2], view[2][2], view[3][2]);
    depth.resize(objects);
    order.resize(objects);
    for (std::uint32_t i = 0; i < objects; ++i) {
      const glm::vec3& c = i < panes ? paneCentres[i] : particleCentres[i - panes];
      depth[i]           = -(row.x * c.x + row.y * c.y + row.z * c.z + row.w);
      order[i]           = i;
    }
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
      return depth[a] != depth[b] ? depth[a] > depth[b] : a < b;
    });

    ids.clear();
    draws.clear();
    for (std::uint32_t object : order) {
      if (object < panes) {
        draws.push_back({ true, object, 0 });
        continue;
      }
      if (draws.empty() || draws.back().pane)
        draws.push_back({ false, static_cast<std::uint32_t>(ids.size()), 0 });
      ids.push_back(object - panes);
      ++draws.back().count;
    }
  }
};

struct Diff {
  int    max     = 0;
  double mean    = 0.0;
  double overOne = 0.0;
  double any     = 0.0;
};

static Diff compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  Diff        d{};
  std::size_t pixels = a.size() / 4, off = 0, differ = 0;
  double      sum    = 0.0;
  for (std::size_t p = 0; p < pixels; ++p) {
    int worst = 0;
    for (std::size_t c = 0; c < 3; ++c) {
      int e = std::abs(static_cast<int>(a[p * 4 + c]) - static_cast<int>(b[p * 4 + c]));
      worst = std::max(worst, e);
      sum += e;
    }
    d.max = std::max(d.max, worst);
    if (worst > 1) ++off;
    if (worst > 0) ++differ;
  }
  d.any     = pixels ? 100.0 * static_cast<double>(differ) / static_cast<double>(pixels) : 0.0;
  d.mean    = pixels ? sum / (3.0 * static_cast<double>(pixels)) : 0.0;
  d.overOne = pixels ? 100.0 * static_cast<double>(off) / static_cast<double>(pixels) : 0.0;
  return d;
}

struct Saturation {
  float       maxAlpha  = 0.0f;
  std::size_t saturated = 0;
  std::size_t nonFinite = 0;
};

static Saturation saturation(const std::vector<float>& accum) {
  Saturation s{};
  for (std::size_t p = 0; p + 3 < accum.size(); p += 4) {
    bool hit = false, bad = false;
    for (std::size_t c = 0; c < 4; ++c) {
      float v = accum[p + c];
      if (!std::isfinite(v)) bad = true;
      if (!std::isfinite(v) || v >= HALF_MAX) hit = true;
    }
    if (std::isfinite(accum[p + 3])) s.maxAlpha = std::max(s.maxAlpha, accum[p + 3]);
    if (hit) ++s.saturated;
    if (bad) ++s.nonFinite;
  }
  return s;
}

static const char* verdict(bool pass) { return pass ? "PASS" : "FAIL"; }

int main(int argc, char** argv) {
  try {
    Args args = parseArgs(argc, argv);

    Method        method    = methodFromName(args.method);
    std::uint32_t particles = args.particles == 0 ? DEFAULT_PARTICLES : args.particles;
    std::uint32_t capacity  = std::max(particles, MANY_PARTICLES);

    GLFWwindow* handle =
      args.headless ? nullptr : createHiddenWindow("Lab 08 - Part IV", WIDTH, HEIGHT);

    Device dev    = createDevice(handle);
    Target target = createTarget(dev, WIDTH, HEIGHT, true);

    Texture scene  = createRenderTexture(dev, WIDTH, HEIGHT, VK_FORMAT_R16G16B16A16_SFLOAT);
    Texture accum  = createRenderTexture(dev, WIDTH, HEIGHT, VK_FORMAT_R16G16B16A16_SFLOAT);
    Texture reveal = createRenderTexture(dev, WIDTH, HEIGHT, VK_FORMAT_R16_SFLOAT);

    Pass opaquePass  = createPass(dev, { { &scene, Load::Clear, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) } },
                                  { &target, Load::Clear });
    Pass overPass    = createPass(dev, { { &scene, Load::Keep } }, { &target, Load::Keep });
    Pass accumPass   = createPass(dev, { { &accum, Load::Clear, glm::vec4(0.0f) },
                                         { &reveal, Load::Clear, glm::vec4(1.0f) } },
                                  { &target, Load::Keep });
    Pass presentPass = createTargetPass(dev, target, Load::Keep);

    Samplers  samplers = createSamplers(dev);
    VkSampler point    = samplers.get(Filter::Nearest, Wrap::Clamp);

    Buffer           uniform = createBuffer(dev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Descriptors      descs   = createDescriptors(dev, uniform, 3);
    VkPipelineLayout layout  = createPipelineLayout(dev, descs, sizeof(glm::vec4));
    bindTexture(dev, descs.sets[1], 0, accum.view, point);
    bindTexture(dev, descs.sets[1], 1, reveal.view, point);
    bindTexture(dev, descs.sets[2], 0, scene.view, point);
    bindTexture(dev, descs.sets[2], 1, accum.view, point);
    bindTexture(dev, descs.sets[2], 2, reveal.view, point);

    glm::mat4 view  = glm::lookAt(EYE, LOOK_AT, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj  = perspectiveReversedZ(FOV_Y, static_cast<float>(WIDTH) / HEIGHT, Z_NEAR, Z_FAR);
    glm::mat4 cloud = glm::inverse(view);

    Mesh opaque = transformed(quad(80.0f, 40.0f),
                              glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 16.0f, WALL_Z)));
    append(opaque, transformed(plane(80.0f, 1.0f),
                               glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, WALL_Z + 40.0f))));
    GpuMesh opaqueMesh = uploadMesh(dev, opaque);
    GpuMesh quadMesh   = uploadMesh(dev, quad(1.0f, 1.0f));

    PaneSet panes{};
    {
      Mesh mesh{};
      for (const PaneSpec& p : PANES) {
        append(mesh, transformed(quad(PANE_W, PANE_H), paneMatrix(p)));
        panes.colors.push_back(glm::vec4(p.color, PANE_ALPHA));
        panes.centres.push_back(p.centre);
      }
      panes.mesh = uploadMesh(dev, mesh);
    }
    PaneSet single{};
    {
      single.mesh    = uploadMesh(dev, transformed(quad(PANE_W, PANE_H), paneMatrix(PANES[0])));
      single.colors  = { glm::vec4(PANES[0].color, PANE_ALPHA) };
      single.centres = { PANES[0].centre };
    }
    PaneSet layers{};
    {
      auto at = [&](float d) {
        return cloud * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, LAYER_UP * TAN_Y * d, -d));
      };
      Mesh mesh{};
      append(mesh, transformed(quad(1.2f, 0.9f), at(NEAR_LAYER_M)));
      append(mesh, transformed(quad(6.0f, 4.5f), at(FAR_LAYER_M)));
      layers.mesh   = uploadMesh(dev, mesh);
      layers.colors = { NEAR_LAYER, FAR_LAYER };
    }

    Buffer ordered  = createBuffer(dev, sizeof(std::uint32_t) * capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Buffer reversed = createBuffer(dev, sizeof(std::uint32_t) * capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Buffer sorted   = createBuffer(dev, sizeof(std::uint32_t) * capacity, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    {
      std::vector<std::uint32_t> ids(capacity);
      std::iota(ids.begin(), ids.end(), 0u);
      uploadBuffer(ordered, ids.data(), sizeof(std::uint32_t) * ids.size());
    }

    Sorter sorter{};
    sorter.paneCentres = panes.centres;
    for (std::uint32_t i = 0; i < capacity; ++i)
      sorter.particleCentres.push_back(glm::vec3(cloud * glm::vec4(particleCentre(i), 1.0f)));

    auto sortFor = [&](const glm::mat4& v, std::uint32_t n) {
      sorter.sort(v, n);
      if (!sorter.ids.empty())
        uploadBuffer(sorted, sorter.ids.data(), sizeof(std::uint32_t) * sorter.ids.size());
      return DrawList{ &panes, &sorted, sorter.draws };
    };

    VertexLayout instLayout = meshLayout();
    instLayout.binding(1, sizeof(std::uint32_t), VK_VERTEX_INPUT_RATE_INSTANCE)
              .attr(4, 1, VK_FORMAT_R32_UINT, 0);

    PipelineState opaqueState{};
    opaqueState.depthCompare = VK_COMPARE_OP_GREATER;
    opaqueState.depthWrite   = true;
    PipelineState overState{};
    overState.depthCompare = VK_COMPARE_OP_GREATER;
    overState.depthWrite   = false;
    overState.blend        = { BlendMode::Over, BlendMode::None };
    PipelineState accumState = overState;
    accumState.blend         = { BlendMode::Additive, BlendMode::Revealage };
    PipelineState heatState  = opaqueState;
    PipelineState compState{};
    compState.blend = { BlendMode::Over, BlendMode::None };

    Program opaqueProg = createProgram(dev, opaquePass, layout, "oit_opaque.vert", "oit_opaque.frag",
                                       meshLayout(), opaqueState);
    Program paneOver   = createProgram(dev, overPass, layout, "oit_pane.vert", "oit_over.frag",
                                       meshLayout(), overState);
    Program smokeOver  = createProgram(dev, overPass, layout, "oit_particle.vert", "oit_over.frag",
                                       instLayout, overState);
    Program paneAccum  = createProgram(dev, accumPass, layout, "oit_pane.vert", "wboit_accum.frag",
                                       meshLayout(), accumState);
    Program smokeAccum = createProgram(dev, accumPass, layout, "oit_particle.vert", "wboit_accum.frag",
                                       instLayout, accumState);
    Program paneHeat   = createProgram(dev, overPass, layout, "oit_pane.vert", "wboit_weightview.frag",
                                       meshLayout(), heatState);
    Program smokeHeat  = createProgram(dev, overPass, layout, "oit_particle.vert", "wboit_weightview.frag",
                                       instLayout, heatState);
    Program composite  = createProgram(dev, overPass, layout, "fullscreen.vert", "wboit_composite.frag",
                                       VertexLayout{}, compState);
    Program present    = createProgram(dev, presentPass, layout, "fullscreen.vert", "oit_present.frag");
    std::array<Program*, 9> programs = { &opaqueProg, &paneOver, &smokeOver, &paneAccum, &smokeAccum,
                                         &paneHeat, &smokeHeat, &composite, &present };

    Params params{};
    params.resolution = glm::vec2(WIDTH, HEIGHT);
    params.time       = args.time;
    params.mode       = args.mode;
    params.knob       = args.knob;
    params.knobf      = args.knobf;
    params.light      = glm::vec4(glm::normalize(glm::vec3(0.35f, 0.8f, 0.5f)), 1.0f);
    params.eye        = glm::vec4(EYE, 1.0f);
    params.model      = cloud;
    params.view       = view;
    params.proj       = proj;

    auto drawList = [&](VkCommandBuffer cmd, const DrawList& list, const Program& pane, const Program& smoke) {
      const Program* bound = nullptr;
      for (const Draw& d : list.draws) {
        const Program& want = d.pane ? pane : smoke;
        if (bound != &want) {
          bindProgram(cmd, want, descs.sets[0]);
          bound = &want;
        }
        if (d.pane) {
          const glm::vec4& c = list.panes->colors[d.first];
          vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                             sizeof(glm::vec4), &c);
          drawMeshRange(cmd, list.panes->mesh, d.first * 6, 6);
        } else {
          bindInstances(cmd, *list.instances, 1);
          drawMesh(cmd, quadMesh, d.count, d.first);
        }
      }
    };
    auto drawOpaque = [&](VkCommandBuffer cmd) {
      beginPass(cmd, opaquePass);
      bindProgram(cmd, opaqueProg, descs.sets[0]);
      drawMesh(cmd, opaqueMesh);
      endPass(cmd);
    };
    auto drawOver = [&](VkCommandBuffer cmd, const DrawList& list) {
      beginPass(cmd, overPass);
      drawList(cmd, list, paneOver, smokeOver);
      endPass(cmd);
    };
    auto drawAccum = [&](VkCommandBuffer cmd, const DrawList& list) {
      beginPass(cmd, accumPass);
      drawList(cmd, list, paneAccum, smokeAccum);
      endPass(cmd);
    };
    auto drawWboit = [&](VkCommandBuffer cmd, const DrawList& list) {
      drawAccum(cmd, list);
      beginPass(cmd, overPass);
      bindProgram(cmd, composite, descs.sets[1]);
      drawFullscreen(cmd);
      endPass(cmd);
    };
    auto drawHeat = [&](VkCommandBuffer cmd, const DrawList& list) {
      beginPass(cmd, overPass);
      drawList(cmd, list, paneHeat, smokeHeat);
      endPass(cmd);
    };
    auto drawPresent = [&](VkCommandBuffer cmd) {
      beginPass(cmd, presentPass);
      bindProgram(cmd, present, descs.sets[2]);
      drawFullscreen(cmd);
      endPass(cmd);
    };
    auto drawFrame = [&](VkCommandBuffer cmd, std::uint32_t mode, Method m, const DrawList& list) {
      drawOpaque(cmd);
      if (mode == 3)                   drawHeat(cmd, list);
      else if (mode == 1 || mode == 2) drawAccum(cmd, list);
      else if (m == Method::Wboit)     drawWboit(cmd, list);
      else                             drawOver(cmd, list);
      drawPresent(cmd);
    };
    auto render = [&](const Params& p, Method m, const DrawList& list) {
      return renderOnce(dev, target, uniform, p,
                        [&](VkCommandBuffer cmd, const Params& q) { drawFrame(cmd, q.mode, m, list); });
    };

    Params fixed = params;
    fixed.mode   = 0;
    fixed.knob   = 0;

    DrawList none{ &panes, &ordered, {} };
    DrawList all = inOrder(panes, ordered, DEFAULT_PARTICLES);
    fillReversed(reversed, DEFAULT_PARTICLES);
    DrawList back = inReverse(panes, reversed, DEFAULT_PARTICLES);

    std::vector<std::uint8_t> opaqueOnly = render(fixed, Method::Wboit, none);
    std::vector<std::uint8_t> wboitImg   = render(fixed, Method::Wboit, all);
    std::vector<std::uint8_t> wboitBack  = render(fixed, Method::Wboit, back);
    std::vector<std::uint8_t> overImg    = render(fixed, Method::Unsorted, all);
    std::vector<std::uint8_t> overBack   = render(fixed, Method::Unsorted, back);
    DrawList                  byDepth    = sortFor(view, DEFAULT_PARTICLES);
    std::vector<std::uint8_t> sortedImg  = render(fixed, Method::Sorted, byDepth);
    Params                    chapter    = fixed;
    chapter.knob                         = 1;
    std::vector<std::uint8_t> chapterImg = render(chapter, Method::Wboit, all);

    DrawList                  one{ &single, &ordered, { { true, 0, 0 } } };
    std::vector<std::uint8_t> oneWboit = render(fixed, Method::Wboit, one);
    std::vector<std::uint8_t> oneOver  = render(fixed, Method::Unsorted, one);

    DrawList twoLayers{ &layers, &ordered, { { true, 0, 0 }, { true, 1, 0 } } };
    render(fixed, Method::Wboit, twoLayers);
    std::vector<float> twoAccum  = readTextureFloats(dev, accum);
    std::vector<float> twoReveal = readTextureFloats(dev, reveal);

    std::array<std::array<Saturation, 2>, 2> sat{};
    for (std::uint32_t knob = 0; knob < 2; ++knob)
      for (std::size_t k = 0; k < 2; ++k) {
        Params p = fixed;
        p.knob   = knob;
        render(p, Method::Wboit, inOrder(panes, ordered, k == 0 ? DEFAULT_PARTICLES : MANY_PARTICLES));
        sat[knob][k] = saturation(readTextureFloats(dev, accum));
      }

    DrawList asked = method == Method::Sorted ? sortFor(view, particles) : inOrder(panes, ordered, particles);
    std::vector<std::uint8_t> rgba = render(params, method, asked);
    Timing timing = timePass(dev, WARMUP_FRAMES, MEASURED_FRAMES,
                             [&](VkCommandBuffer cmd) { drawFrame(cmd, params.mode, method, asked); });

    struct Cell {
      std::uint32_t n        = 0;
      double        unsorted = 0.0, sortCpu = 0.0, sortedGpu = 0.0, wboit = 0.0;
    };
    constexpr std::size_t ROUNDS = 3;
    std::array<Cell, 2>   cells{};
    cells[0].n = DEFAULT_PARTICLES;
    cells[1].n = MANY_PARTICLES;
    {
      std::array<std::array<std::array<double, ROUNDS>, 4>, 2> rounds{};
      uploadBuffer(uniform, &fixed, sizeof(Params));
      submitNow(dev, [&](VkCommandBuffer cmd) { drawOpaque(cmd); });
      DrawList heavy = inOrder(panes, ordered, MANY_PARTICLES);
      timePass(dev, SWEEP_WARMUP, 1, [&](VkCommandBuffer cmd) { drawWboit(cmd, heavy); });
      for (std::size_t r = 0; r < ROUNDS; ++r)
        for (std::size_t c = 0; c < cells.size(); ++c) {
          std::uint32_t n    = cells[c].n;
          DrawList      list = inOrder(panes, ordered, n);
          DrawList      byZ  = sortFor(view, n);
          rounds[c][0][r] = timePass(dev, SWEEP_WARMUP, MEASURED_FRAMES,
                                     [&](VkCommandBuffer cmd) { drawOver(cmd, list); }).medianMs;
          rounds[c][1][r] = timeCpu(2, MEASURED_FRAMES, [&] { sorter.sort(view, n); }).medianMs;
          rounds[c][2][r] = timePass(dev, SWEEP_WARMUP, MEASURED_FRAMES,
                                     [&](VkCommandBuffer cmd) { drawOver(cmd, byZ); }).medianMs;
          rounds[c][3][r] = timePass(dev, SWEEP_WARMUP, MEASURED_FRAMES,
                                     [&](VkCommandBuffer cmd) { drawWboit(cmd, list); }).medianMs;
        }
      for (std::size_t c = 0; c < cells.size(); ++c) {
        double* out[4] = { &cells[c].unsorted, &cells[c].sortCpu, &cells[c].sortedGpu, &cells[c].wboit };
        for (std::size_t k = 0; k < 4; ++k) {
          std::array<double, ROUNDS> v = rounds[c][k];
          std::sort(v.begin(), v.end());
          *out[k] = v[ROUNDS / 2];
        }
      }
    }

    std::cout << std::fixed;
    std::cout << label("shaders") << "oit_opaque.vert/.frag; oit_pane.vert, oit_particle.vert into\n"
              << indent() << "oit_over.frag or wboit_accum.frag (both include transparent.glsl);\n"
              << indent() << "wboit_composite.frag; oit_present.frag (includes color.glsl)\n";
    std::cout << std::setprecision(0);
    std::cout << label("scene") << "opaque wall and floor; 3 glass panes " << PANE_W << " x " << PANE_H
              << " m, alpha " << std::setprecision(2) << PANE_ALPHA << ", cutting\n"
              << indent() << "through each other; " << DEFAULT_PARTICLES << " smoke particles "
              << std::setprecision(0) << CLOUD_NEAR << " .. " << CLOUD_FAR << " m from the camera\n";
    std::cout << label("method") << methodName(method) << ", " << particles << " particles"
              << (params.knob == 1 ? ", the chapter's weight (window depth)" : "") << "\n";

    bool wboitDrew = compare(wboitImg, opaqueOnly).max > 0;

    {
      Diff d    = compare(oneWboit, oneOver);
      bool drew = compare(oneWboit, opaqueOnly).max > 1;
      bool ok   = d.max <= 1 && drew;
      std::cout << label("single layer") << "one pane over the scene: max |wboit - over| " << d.max
                << (d.max == 1 ? " level   " : " levels   ") << verdict(ok) << " (<= 1)\n";
      if (!drew) std::cout << indent() << "(WBOIT drew nothing: the picture is the opaque scene)\n";
    }

    {
      std::size_t i      = std::size_t(LAYER_PIXEL.y) * WIDTH + LAYER_PIXEL.x;
      double      a      = twoAccum[i * 4 + 3];
      double      share  = a > 0.0 ? twoAccum[i * 4 + 0] / a : 0.0;
      double      rev    = twoReveal[i];
      double      aNear  = NEAR_LAYER.a, aFar = FAR_LAYER.a;
      double      wNear  = weightOf(NEAR_LAYER_M), wFar = weightOf(FAR_LAYER_M);
      double      cpu    = aNear * wNear / (aNear * wNear + aFar * wFar);
      double      flat   = aNear / (aNear + aFar);
      double      cpuRev = (1.0 - aNear) * (1.0 - aFar);
      bool        ok     = a > 0.0 && std::abs(share - cpu) < 0.01 && std::abs(rev - cpuRev) < 0.005;
      std::cout << label("two layers") << "red alpha " << std::setprecision(2) << aNear << " at "
                << std::setprecision(0) << NEAR_LAYER_M << " m in front of blue alpha "
                << std::setprecision(2) << aFar << " at " << std::setprecision(0) << FAR_LAYER_M
                << " m, pixel (" << LAYER_PIXEL.x << ", " << LAYER_PIXEL.y << ")\n";
      std::cout << std::setprecision(3);
      std::cout << indent() << "red share of accum   yours " << share << "   cpu " << cpu
                << "   (one weight for all " << flat << ")\n";
      std::cout << indent() << "revealage            yours " << rev << "   cpu " << cpuRev << "   "
                << verdict(ok) << "\n";
    }

    {
      Diff w  = compare(wboitImg, wboitBack);
      Diff o  = compare(overImg, overBack);
      bool ok = wboitDrew && w.overOne < 0.1;
      std::cout << std::setprecision(2);
      std::cout << label("order") << "transparent list drawn in reverse: wboit differs on " << w.any
                << "% of pixels,\n"
                << indent() << "by at most " << w.max << (w.max == 1 ? " level" : " levels") << " ("
                << w.overOne << "% by more than 1)   " << verdict(ok)
                << (wboitDrew ? "" : " (WBOIT drew nothing)") << "\n";
      std::cout << indent() << "unsorted Over reversed the same way: " << o.any << "% of pixels, by up to "
                << o.max << "\n";
    }

    {
      Diff s = compare(wboitImg, sortedImg);
      Diff c = compare(chapterImg, sortedImg);
      Diff u = compare(overImg, sortedImg);
      std::cout << std::setprecision(1);
      std::cout << label("vs sorted") << "mean |wboit - sorted| " << s.mean << " levels, max " << s.max
                << " (report only)\n";
      std::cout << indent() << "the same with the chapter's weight: mean " << c.mean << ", max " << c.max << "\n";
      std::cout << indent() << "mean |unsorted - sorted| " << u.mean << " levels, max " << u.max << "\n";
    }

    {
      float dMin = std::numeric_limits<float>::max(), dMax = 0.0f;
      for (const PaneSpec& p : PANES)
        for (float sx : { -0.5f, 0.5f })
          for (float sy : { -0.5f, 0.5f }) {
            float d = -(view * paneMatrix(p) * glm::vec4(sx * PANE_W, sy * PANE_H, 0.0f, 1.0f)).z;
            dMin    = std::min(dMin, d);
            dMax    = std::max(dMax, d);
          }
      for (std::uint32_t i = 0; i < DEFAULT_PARTICLES; ++i) {
        float d = -(view * glm::vec4(sorter.particleCentres[i], 1.0f)).z;
        dMin    = std::min(dMin, d);
        dMax    = std::max(dMax, d);
      }
      auto windowDepth = [&](float d) {
        glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, -d, 1.0f);
        return static_cast<double>(clip.z / clip.w);
      };
      double zNear = windowDepth(dMin), zFar = windowDepth(dMax);
      double cLo = std::min(weightOf(zNear), weightOf(zFar)), cHi = std::max(weightOf(zNear), weightOf(zFar));
      std::cout << std::setprecision(1);
      std::cout << label("weight") << "w over the transparent objects, " << dMin << " .. " << dMax
                << " m from the camera\n";
      glm::mat4 usual    = glm::perspective(FOV_Y, static_cast<float>(WIDTH) / HEIGHT, Z_NEAR, Z_FAR);
      auto      usualZ   = [&](float d) {
        glm::vec4 clip = usual * glm::vec4(0.0f, 0.0f, -d, 1.0f);
        return static_cast<double>(clip.z / clip.w);
      };
      double uNear = usualZ(dMin), uFar = usualZ(dMax);
      std::cout << indent() << "chapter, window depth " << std::setprecision(4) << zFar << " .. " << zNear
                << "   w " << std::setprecision(1) << cLo << " .. " << cHi << "\n";
      std::cout << indent() << "view depth in metres                  w " << weightOf(dMax) << " .. "
                << weightOf(dMin) << "\n";
      std::cout << indent() << "(a depth buffer with 0 near, 1 far: z " << std::setprecision(4) << uNear
                << " .. " << uFar << ", w " << std::setprecision(1) << std::min(weightOf(uNear), weightOf(uFar))
                << " .. " << std::max(weightOf(uNear), weightOf(uFar)) << ")\n";
    }

    {
      std::cout << label("fp16 accum") << "largest accum.a, and how many pixels have a channel at 65504\n"
                << indent() << "(the largest fp16) or at Inf / NaN\n";
      std::cout << indent() << "weight on" << std::setw(20) << std::to_string(DEFAULT_PARTICLES) + " particles"
                << std::setw(20) << std::to_string(MANY_PARTICLES) + " particles" << "\n";
      const char* names[2] = { "view depth   ", "window depth " };
      std::size_t nonFinite = 0;
      for (std::size_t knob = 0; knob < 2; ++knob) {
        std::cout << indent() << names[knob];
        for (std::size_t k = 0; k < 2; ++k) {
          const Saturation& s = sat[knob][k];
          nonFinite += s.nonFinite;
          std::ostringstream cell;
          cell << std::fixed << std::setprecision(0) << s.maxAlpha << " (" << s.saturated << " px)";
          std::cout << std::setw(k == 0 ? 16 : 20) << cell.str();
        }
        std::cout << "\n";
      }
      std::cout << indent() << "Inf or NaN: "
                << (nonFinite == 0 ? std::string("none") : std::to_string(nonFinite) + " px") << "\n";
    }

    {
      std::cout << label("timing") << "GPU ms of the transparent passes (median of " << ROUNDS
                << " rounds); sort on the CPU\n";
      std::cout << indent() << "particles   unsorted   sorted: cpu sort + gpu   wboit: both passes\n";
      std::cout << std::setprecision(3);
      for (const Cell& c : cells)
        std::cout << indent() << std::setw(9) << c.n << std::setw(11) << c.unsorted << std::setw(14)
                  << c.sortCpu << " + " << std::setw(6) << c.sortedGpu << std::setw(15) << c.wboit << "\n";
    }

    bool   standard = method == Method::Wboit && particles == DEFAULT_PARTICLES && params.knob == 0;
    double diff     = standard ? diffFromReference("oit", rgba, target.width, target.height) : -1.0;
    std::cout << label("image")
              << (standard ? imageResult(diff) : std::string("no reference for these flags (it is wboit, ") +
                                                   std::to_string(DEFAULT_PARTICLES) + " particles, knob 0)")
              << "\n";

    if (shaderHasMarker("wboit_accum.frag", "TODO(TASK 4a)"))
      std::cout << label("incomplete") << "wboit_accum.frag still has TODO(TASK 4a): it adds no colour and\n"
                << indent() << "covers nothing, so WBOIT shows the opaque scene unchanged\n";
    if (shaderHasMarker("wboit_composite.frag", "TODO(TASK 4b)"))
      std::cout << label("incomplete") << "wboit_composite.frag still has TODO(TASK 4b): it outputs alpha 0,\n"
                << indent() << "so the composite leaves the opaque scene unchanged\n";

    std::cout << std::setprecision(3);
    std::cout << label("gpu time") << timing.medianMs << " ms median, " << timing.p95Ms
              << " ms p95 (whole frame, " << methodName(method) << ")" << std::endl;

    if (!args.save.empty()) writePng(args.save, rgba, target.width, target.height);

    if (!args.headless) {
      Window win = createSwapchain(dev, handle);

      OrbitCamera cam      = OrbitCamera::looking(EYE, LOOK_AT);
      bool        backward = false;
      DrawList    current  = asked;
      auto        rebuild  = [&](const Params& p) {
        if (method == Method::Sorted) {
          current = sortFor(p.view, particles);
        } else if (backward) {
          fillReversed(reversed, particles);
          current = inReverse(panes, reversed, particles);
        } else {
          current = inOrder(panes, ordered, particles);
        }
      };

      FrameHooks hooks{};
      hooks.camera  = &cam;
      hooks.record  = [&](VkCommandBuffer cmd, const Params& p) { drawFrame(cmd, p.mode, method, current); };
      hooks.onFrame = [&](Params& p) {
        if (method == Method::Sorted) current = sortFor(p.view, particles);
      };
      hooks.onKey = [&](int key, Params& p) {
        if (key == GLFW_KEY_M) {
          method = static_cast<Method>((static_cast<std::uint32_t>(method) + 1) % 3);
          std::cout << label("method") << methodName(method) << std::endl;
        } else if (key == GLFW_KEY_W) {
          p.knob = p.knob == 1 ? 0 : 1;
          std::cout << label("weight") << (p.knob == 1 ? "the chapter's: window depth gl_FragCoord.z"
                                                       : "view depth in metres") << std::endl;
        } else if (key == GLFW_KEY_P) {
          particles = particles == MANY_PARTICLES ? DEFAULT_PARTICLES : MANY_PARTICLES;
          std::cout << label("particles") << particles << std::endl;
        } else if (key == GLFW_KEY_R) {
          backward = !backward;
          std::cout << label("draw order") << (backward ? "reversed" : "as generated")
                    << (method == Method::Sorted ? " (sorted ignores it)" : "") << std::endl;
        } else {
          return;
        }
        rebuild(p);
      };
      hooks.shaders   = { "oit_opaque.vert", "oit_opaque.frag", "oit_pane.vert", "oit_particle.vert",
                          "oit_over.frag", "wboit_accum.frag", "wboit_weightview.frag", "fullscreen.vert",
                          "wboit_composite.frag", "oit_present.frag" };
      hooks.savePath  = std::string(REPORT_DIR) + "/oit.png";
      hooks.extraKeys = "mode 0 picture, 1 revealage (magenta: fp16 accum saturated),\n"
                        "  2 accum.a heat 1 .. 65504, 3 weight of the front-most layer (blue 30, red 3000)\n"
                        "M method: unsorted, sorted (back to front per object), wboit\n"
                        "W weight on view depth in metres / on gl_FragCoord.z, as the chapter\n"
                        "P particles 1000 / 10000\n"
                        "R reverse the draw order (unsorted changes, wboit should not)";
      hooks.onReload  = [&](const std::vector<std::string>&) {
        for (Program* p : programs) reloadProgram(dev, *p);
      };

      runWindow(dev, win, target, uniform, params, args, hooks);
      destroySwapchain(dev, win);
    }

    for (Program* p : programs) destroyProgram(dev, *p);
    vkDestroyPipelineLayout(dev.device, layout, nullptr);
    destroyDescriptors(dev, descs);
    destroyBuffer(dev, sorted);
    destroyBuffer(dev, reversed);
    destroyBuffer(dev, ordered);
    destroyBuffer(dev, uniform);
    destroyMesh(dev, layers.mesh);
    destroyMesh(dev, single.mesh);
    destroyMesh(dev, panes.mesh);
    destroyMesh(dev, quadMesh);
    destroyMesh(dev, opaqueMesh);
    destroySamplers(dev, samplers);
    destroyPass(dev, presentPass);
    destroyPass(dev, accumPass);
    destroyPass(dev, overPass);
    destroyPass(dev, opaquePass);
    destroyTexture(dev, reveal);
    destroyTexture(dev, accum);
    destroyTexture(dev, scene);
    destroyTarget(dev, target);
    destroyDevice(dev);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
