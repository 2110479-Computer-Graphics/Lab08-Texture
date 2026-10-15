#include "core/Frame.h"
#include "core/Geometry.h"
#include "core/Sampler.h"
#include "core/Shader.h"
#include "core/Timer.h"
#include "utils/Color.h"
#include "utils/Mesh.h"
#include "utils/image.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static constexpr double PI = 3.14159265358979323846;

static constexpr std::uint32_t RINGS     = 64;
static constexpr std::uint32_t SEGMENTS  = 128;
static constexpr float         MODEL_YAW = 0.5f;
static const glm::vec3         EYE       = { 0.0f, 0.4f, 3.0f };
static const glm::vec2         TILES     = { 8.0f, 4.0f };
static constexpr float         FOV_Y     = 45.0f;

static constexpr float LIGHT_Y     = 1.0f;
static constexpr float LIGHT_ANGLE = 0.35f;
static constexpr float LIGHT_SPEED = 0.8f;

static glm::vec3 lightAt(float angle) {
  return glm::normalize(glm::vec3(std::cos(angle), LIGHT_Y, std::sin(angle)));
}

static constexpr std::uint32_t MAP    = 512;
static constexpr std::uint32_t CELLS  = 8;
static constexpr double        BUMP_R = 0.4;
static constexpr double        BUMP_H = 0.3;

struct Bump {
  double h  = 0.0;
  double dx = 0.0;
  double dy = 0.0;
};

static Bump bump(double cx, double cy) {
  double q = (cx * cx + cy * cy) / (BUMP_R * BUMP_R);
  if (q >= 1.0) return {};
  double k = 1.0 - q;
  double g = -4.0 * k / (BUMP_R * BUMP_R);
  return { BUMP_H * k * k, BUMP_H * g * cx, BUMP_H * g * cy };
}

static double cellOffset(double t) {
  double c = t * CELLS;
  return c - std::floor(c) - 0.5;
}

static std::uint8_t encode(double n) {
  return static_cast<std::uint8_t>(toByte(n * 0.5 + 0.5));
}

static Image makeNormalMap() {
  Image image(MAP, MAP);
  for (std::uint32_t y = 0; y < MAP; ++y)
    for (std::uint32_t x = 0; x < MAP; ++x) {
      Bump b = bump(cellOffset((x + 0.5) / MAP), cellOffset((y + 0.5) / MAP));
      glm::dvec3 n = glm::normalize(glm::dvec3(-b.dx, b.dy, 1.0));
      std::uint8_t* t = image.at(x, y);
      t[0] = encode(n.x);
      t[1] = encode(n.y);
      t[2] = encode(n.z);
      t[3] = 255;
    }
  return image;
}

static Image flipGreen(const Image& map) {
  Image flipped = map;
  for (std::size_t i = 1; i < flipped.rgba.size(); i += 4)
    flipped.rgba[i] = static_cast<std::uint8_t>(255 - flipped.rgba[i]);
  return flipped;
}

static Image makeAlbedo() {
  Image image(4, 4);
  for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
    image.rgba[i] = image.rgba[i + 1] = image.rgba[i + 2] = 200;
    image.rgba[i + 3] = 255;
  }
  return image;
}

static double meanLength(const Image& level) {
  double sum = 0.0;
  for (std::size_t i = 0; i < level.rgba.size(); i += 4) {
    glm::dvec3 n(level.rgba[i] / 255.0, level.rgba[i + 1] / 255.0, level.rgba[i + 2] / 255.0);
    sum += glm::length(n * 2.0 - 1.0);
  }
  return sum / static_cast<double>(level.rgba.size() / 4);
}

static const double EPS = PI / (static_cast<double>(TILES.y) * CELLS);

struct View {
  glm::dmat4 model;
  glm::dmat4 viewProj;
  glm::dvec3 eye;
};

static glm::dvec2 project(const View& view, glm::dvec3 world) {
  glm::dvec4 clip = view.viewProj * glm::dvec4(world, 1.0);
  glm::dvec2 ndc  = glm::dvec2(clip) / clip.w;
  return glm::dvec2((ndc.x * 0.5 + 0.5) * WIDTH, (ndc.y * 0.5 + 0.5) * HEIGHT);
}

static glm::dvec3 spherePoint(double phi, double theta) {
  return glm::dvec3(std::sin(theta) * std::sin(phi), std::cos(theta), std::sin(theta) * std::cos(phi));
}

static glm::dvec2 castPixel(const View& view, std::uint32_t x, std::uint32_t y) {
  double     nx    = (x + 0.5) / WIDTH * 2.0 - 1.0;
  double     ny    = (y + 0.5) / HEIGHT * 2.0 - 1.0;
  glm::dvec4 point = glm::inverse(view.viewProj) * glm::dvec4(nx, ny, 0.5, 1.0);
  glm::dvec3 dir   = glm::dvec3(point) / point.w - view.eye;

  glm::dmat4 toObject = glm::inverse(view.model);
  glm::dvec3 o = glm::dvec3(toObject * glm::dvec4(view.eye, 1.0));
  glm::dvec3 d = glm::normalize(glm::dvec3(toObject * glm::dvec4(dir, 0.0)));
  double     b = glm::dot(o, d);
  double     c = glm::dot(o, o) - 1.0;
  glm::dvec3 p = o + (-b - std::sqrt(std::max(b * b - c, 0.0))) * d;

  double phi = std::atan2(p.x, p.z);
  if (phi < 0.0) phi += 2.0 * PI;
  return glm::dvec2(phi, std::acos(std::clamp(p.y, -1.0, 1.0)));
}

static glm::dvec3 trueNormal(const View& view, double phi, double theta) {
  Bump b = bump(cellOffset(phi / (2.0 * PI) * TILES.x), cellOffset(theta / PI * TILES.y));

  double perPhi   = TILES.x * CELLS / (2.0 * PI);
  double perTheta = TILES.y * CELLS / PI;

  glm::dvec3 p      = spherePoint(phi, theta);
  glm::dvec3 dPdphi = glm::dvec3(std::sin(theta) * std::cos(phi), 0.0, -std::sin(theta) * std::sin(phi));
  glm::dvec3 dPdth  = glm::dvec3(std::cos(theta) * std::sin(phi), -std::sin(theta),
                                 std::cos(theta) * std::cos(phi));

  double     grow   = 1.0 + EPS * b.h;
  glm::dvec3 dphi   = grow * dPdphi + EPS * b.dx * perPhi * p;
  glm::dvec3 dtheta = grow * dPdth + EPS * b.dy * perTheta * p;
  glm::dvec3 n      = glm::normalize(glm::cross(dphi, dtheta));
  if (glm::dot(n, p) < 0.0) n = -n;
  return glm::normalize(glm::dvec3(view.model * glm::dvec4(n, 0.0)));
}

struct Probe {
  std::uint32_t x      = 0;
  std::uint32_t y      = 0;
  const char*   facing = "";
  glm::dvec3    truth{0.0};
};

static std::array<Probe, 6> makeProbes(const View& view) {
  static const std::array<const char*, 6> FACING = {
    "right", "up-right", "up-left", "left", "down-left", "down-right"
  };
  static const std::array<glm::ivec2, 6> CELL = {{ { 2, 1 }, { 2, -1 }, { 0, -1 },
                                                   { -2, -1 }, { -2, 1 }, { 0, 1 } }};

  glm::dvec3 middle   = glm::dvec3(glm::inverse(view.model) * glm::dvec4(glm::normalize(view.eye), 0.0));
  double     cellPhi  = 2.0 * PI / (TILES.x * CELLS);
  double     cellTh   = PI / (TILES.y * CELLS);
  double     phi0     = std::atan2(middle.x, middle.z);
  double     cellU    = std::floor(phi0 / cellPhi);
  double     cellV    = std::floor(std::acos(middle.y) / cellTh);

  std::array<Probe, 6> probes{};
  for (std::size_t k = 0; k < probes.size(); ++k) {
    double angle = static_cast<double>(k) * PI / 3.0;
    double r     = 0.55 * BUMP_R;
    double cu    = cellU + CELL[k].x + 0.5 + r * std::cos(angle);
    double cv    = cellV + CELL[k].y + 0.5 - r * std::sin(angle);
    glm::dvec3 world = glm::dvec3(view.model * glm::dvec4(spherePoint(cu * cellPhi, cv * cellTh), 1.0));
    glm::dvec2 pixel = project(view, world);

    Probe& probe = probes[k];
    probe.x      = static_cast<std::uint32_t>(std::floor(pixel.x));
    probe.y      = static_cast<std::uint32_t>(std::floor(pixel.y));
    probe.facing = FACING[k];
    glm::dvec2 at = castPixel(view, probe.x, probe.y);
    probe.truth  = trueNormal(view, at.x, at.y);
  }
  return probes;
}

static glm::dvec3 decodeNormal(const std::vector<std::uint8_t>& rgba, std::uint32_t x, std::uint32_t y) {
  std::vector<std::uint8_t> c = pixelAt(rgba, WIDTH, x, y);
  glm::dvec3 n(c[0] / 255.0 * 2.0 - 1.0, c[1] / 255.0 * 2.0 - 1.0, c[2] / 255.0 * 2.0 - 1.0);
  double len = glm::length(n);
  return len > 0.0 ? n / len : n;
}

static double differPct(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b,
                        const std::vector<bool>& mask)
{
  std::size_t covered = 0, differ = 0;
  for (std::size_t p = 0; p < mask.size(); ++p) {
    if (!mask[p]) continue;
    ++covered;
    for (std::size_t c = 0; c < 3; ++c)
      if (std::abs(static_cast<int>(a[p * 4 + c]) - static_cast<int>(b[p * 4 + c])) > 3) {
        ++differ;
        break;
      }
  }
  return covered ? 100.0 * static_cast<double>(differ) / static_cast<double>(covered) : 0.0;
}

static const char* verdict(bool pass) { return pass ? "PASS" : "FAIL"; }

static std::string mapName(bool own, bool dx) {
  if (own) return dx ? "your map with green inverted" : "your map";
  return dx ? "DirectX map" : "OpenGL map";
}

static constexpr double PROBE_MIN      = 0.98;
static constexpr double SAME_MAX_PCT   = 0.5;
static constexpr double DIFFER_MIN_PCT = 10.0;

int main(int argc, char** argv) {
  try {
    Args args = parseArgs(argc, argv);

    GLFWwindow* handle =
      args.headless ? nullptr : createHiddenWindow("Lab 08 - Part II", WIDTH, HEIGHT);

    Device dev    = createDevice(handle);
    Target target = createTarget(dev, WIDTH, HEIGHT, true);
    Pass   pass   = createTargetPass(dev, target);

    bool               own     = !args.normal.empty();
    Image              glMap   = own ? loadPng(args.normal) : makeNormalMap();
    std::vector<Image> glChain = boxMips(glMap);
    std::vector<Image> dxChain = boxMips(flipGreen(glMap));
    Texture glTex  = createTexture(dev, glChain, VK_FORMAT_R8G8B8A8_UNORM);
    Texture dxTex  = createTexture(dev, dxChain, VK_FORMAT_R8G8B8A8_UNORM);
    Texture albedo = createTexture(dev, boxMips(makeAlbedo()), VK_FORMAT_R8G8B8A8_SRGB);

    Samplers  samplers  = createSamplers(dev);
    VkSampler trilinear = samplers.get(Filter::Trilinear, Wrap::Repeat);

    Buffer uniform = createBuffer(dev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Descriptors      descs  = createDescriptors(dev, uniform);
    VkPipelineLayout layout = createPipelineLayout(dev, descs);
    bindTexture(dev, descs.set, 0, albedo.view, trilinear);

    auto bindNormals = [&](bool dx, bool srgb) {
      const Texture& map = dx ? dxTex : glTex;
      bindTexture(dev, descs.set, 1, srgb ? map.srgbView : map.unormView, trilinear);
    };

    PipelineState state{};
    state.depthCompare = VK_COMPARE_OP_GREATER;
    state.depthWrite   = true;
    state.cullMode     = VK_CULL_MODE_BACK_BIT;
    Program prog = createProgram(dev, pass, layout, "normalmap.vert", "normalmap.frag", meshLayout(),
                                 state);

    Mesh sphereMesh = uvSphere(RINGS, SEGMENTS);
    for (Vertex& v : sphereMesh.vertices) v.uv *= TILES;
    GpuMesh sphere = uploadMesh(dev, sphereMesh);

    Params params{};
    params.resolution = glm::vec2(WIDTH, HEIGHT);
    params.time       = args.time;
    params.mode       = args.mode;
    params.knob       = args.knob;
    params.knobf      = args.knobf;
    params.light      = glm::vec4(lightAt(LIGHT_ANGLE), 1.0f);
    params.eye        = glm::vec4(EYE, 1.0f);
    params.texSize    = glm::vec2(glMap.width, glMap.height);
    params.model      = glm::rotate(glm::mat4(1.0f), MODEL_YAW, glm::vec3(0.0f, 1.0f, 0.0f));
    params.view       = glm::lookAt(EYE, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    params.proj       = perspectiveReversedZ(glm::radians(FOV_Y),
                                             static_cast<float>(WIDTH) / static_cast<float>(HEIGHT),
                                             0.1f, 100.0f);

    auto record = [&](VkCommandBuffer cmd, const Params&) {
      beginPass(cmd, pass);
      bindProgram(cmd, prog, descs.set);
      drawMesh(cmd, sphere);
      endPass(cmd);
    };

    Params check = params;
    check.mode   = 1;
    check.knob   = 0;
    bindNormals(false, false);
    std::vector<std::uint8_t> glNormals = renderOnce(dev, target, uniform, check, record);
    bindNormals(true, false);
    check.knob = 1;
    std::vector<std::uint8_t> dxFlipped = renderOnce(dev, target, uniform, check, record);
    check.knob = 0;
    std::vector<std::uint8_t> dxPlain = renderOnce(dev, target, uniform, check, record);

    bindNormals(args.dx, args.srgbNormals);
    std::vector<std::uint8_t> rgba = renderOnce(dev, target, uniform, params, record);
    Timing timing = timePass(dev, WARMUP_FRAMES, MEASURED_FRAMES,
                             [&](VkCommandBuffer cmd) { record(cmd, params); });

    std::cout << std::fixed << std::setprecision(2);
    std::cout << label("shaders") << "normalmap.vert + normalmap.frag, which includes color.glsl\n";
    std::cout << label("scene") << "uv sphere " << RINGS << " x " << SEGMENTS << ", radius 1, turned "
              << MODEL_YAW << " rad about +y, camera at (" << EYE.x << ", " << EYE.y << ", " << EYE.z
              << ")\n";
    std::cout << label("normal map") << glMap.width << "x" << glMap.height
              << (own ? " from " + args.normal : std::string(" generated, 8 x 8 round bumps"))
              << std::setprecision(0) << ", tiled " << TILES.x << " x " << TILES.y
              << " around the sphere\n"
              << indent() << "tex1 UNORM, trilinear, " << glChain.size()
              << " levels box-filtered on the CPU; DirectX twin = green inverted\n";
    glm::vec3 L = lightAt(LIGHT_ANGLE);
    std::cout << std::setprecision(2) << label("light") << "toward (" << L.x << ", " << L.y << ", "
              << L.z << "), angle " << LIGHT_ANGLE << " rad on its circle\n";
    std::cout << label("measured") << mapName(own, args.dx) << ", "
              << (args.srgbNormals ? "SRGB" : "UNORM") << " view, knob " << params.knob << ", mode "
              << params.mode;
    if (params.mode == 3) std::cout << " at lod " << params.knobf;
    std::cout << "\n";

    if (own) {
      std::cout << label("probes") << "n/a: --normal replaced the bumps the probes are computed from\n";
    } else {
      View view{};
      view.model    = glm::dmat4(params.model);
      view.viewProj = glm::dmat4(params.proj) * glm::dmat4(params.view);
      view.eye      = glm::dvec3(EYE);
      std::cout << label("probes") << "your world normal (mode 1) on six bump flanks against the cpu's:\n"
                << indent() << "the sphere displaced by the same heights, PASS at dot > " << PROBE_MIN
                << "\n";
      for (const Probe& probe : makeProbes(view)) {
        glm::dvec3 yours = decodeNormal(glNormals, probe.x, probe.y);
        double     d     = glm::dot(yours, probe.truth);
        std::ostringstream at;
        at << "(" << probe.x << ", " << probe.y << ")";
        std::cout << std::setprecision(2) << label("probe") << std::left << std::setw(11) << at.str()
                  << std::setw(12) << probe.facing << std::right << "cpu (" << std::setw(5)
                  << probe.truth.x << "," << std::setw(6) << probe.truth.y << "," << std::setw(6)
                  << probe.truth.z << ")  yours (" << std::setw(5) << yours.x << "," << std::setw(6)
                  << yours.y << "," << std::setw(6) << yours.z << ")  dot " << std::setprecision(4)
                  << d << "   " << verdict(d > PROBE_MIN) << "\n";
      }
    }

    std::vector<bool> onSphere(std::size_t(WIDTH) * HEIGHT);
    for (std::size_t p = 0; p < onSphere.size(); ++p)
      onSphere[p] = glNormals[p * 4] != 0 || glNormals[p * 4 + 1] != 0 || glNormals[p * 4 + 2] != 0;
    double sameDiff  = differPct(dxFlipped, glNormals, onSphere);
    double plainDiff = differPct(dxPlain, glNormals, onSphere);
    bool   mapUsed   = plainDiff > DIFFER_MIN_PCT;
    std::cout << std::setprecision(2);
    std::cout << label("convention")
              << (own ? "twin + flip (knob 1) vs your map  " : "DX map + flip (knob 1) vs GL map  ")
              << std::setw(6) << sameDiff
              << "% of the sphere differs   ";
    bool sameOk = sameDiff < SAME_MAX_PCT;
    if (!mapUsed) std::cout << "FAIL (the map changes nothing)\n";
    else std::cout << verdict(sameOk) << (sameOk ? " (< " : " (should be < ") << std::setprecision(1)
                   << SAME_MAX_PCT << "%)\n";
    std::cout << std::setprecision(2) << label("no flip")
              << (own ? "twin, knob 0 vs your map          " : "DX map, knob 0 vs GL map          ")
              << std::setw(6) << plainDiff << "% of the sphere differs   " << verdict(mapUsed)
              << (mapUsed ? " (> " : " (should be > ") << std::setprecision(0) << DIFFER_MIN_PCT << "%)\n";

    std::cout << std::setprecision(4) << label("mean |n|");
    const char* gap = "";
    for (std::size_t level : { 0, 2, 4, 6 })
      if (level < glChain.size()) {
        std::cout << gap << "mip " << level << "  " << meanLength(glChain[level]);
        gap = "   ";
      }
    std::cout << "\n" << indent() << "n = byte / 255 * 2 - 1 over every texel of the level (cpu)\n";

    double diff = own ? -1.0 : diffFromReference("normalmap", rgba, target.width, target.height);
    std::cout << label("image") << (own ? std::string("no reference for --normal") : imageResult(diff))
              << "\n";

    if (shaderHasMarker("normalmap.vert", "TODO(TASK 2a)"))
      std::cout << label("incomplete") << "normalmap.vert still has TODO(TASK 2a): N, T and B all\n"
                << indent() << "leave as the object-space normal, so there is no tangent frame\n";
    if (shaderHasMarker("normalmap.frag", "TODO(TASK 2b)"))
      std::cout << label("incomplete") << "normalmap.frag still has TODO(TASK 2b): N is the geometric\n"
                << indent() << "normal, so the bumps are missing\n";
    if (shaderHasMarker("color.glsl", "TODO(TASK 1b)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1b): linear_to_srgb returns c,\n"
                << indent() << "so mode 0 is not encoded for the screen\n";

    std::cout << std::setprecision(3);
    std::cout << label("gpu time") << timing.medianMs << " ms median, " << timing.p95Ms
              << " ms p95" << std::endl;

    if (!args.save.empty()) writePng(args.save, rgba, target.width, target.height);

    if (!args.headless) {
      Window win = createSwapchain(dev, handle);

      OrbitCamera cam      = OrbitCamera::looking(EYE, glm::vec3(0.0f));
      bool        dx       = args.dx;
      bool        srgb     = args.srgbNormals;
      bool        paused   = false;
      bool        first    = true;
      float       angle    = LIGHT_ANGLE;
      float       lastTime = 0.0f;
      float       maxLod   = static_cast<float>(glChain.size() - 1);

      auto status = [&](const Params& p) {
        std::cout << std::setprecision(1) << label("now") << mapName(own, dx) << ", "
                  << (srgb ? "SRGB" : "UNORM") << " view, knob " << p.knob
                  << (p.knob == 1 ? " (flip green)" : " (no flip)") << ", lod " << p.knobf
                  << " (mode 3), light " << (paused ? "paused" : "moving") << std::endl;
      };

      FrameHooks hooks{};
      hooks.record    = record;
      hooks.shaders   = { "normalmap.vert", "normalmap.frag" };
      hooks.savePath  = std::string(REPORT_DIR) + "/normalmap.png";
      hooks.camera    = &cam;
      hooks.extraKeys =
        "C binds the OpenGL map or its DirectX twin (green inverted)\n"
        "K toggles the knob: 0 reads the map as OpenGL, 1 as DirectX (flip green)\n"
        "G binds the normal map through its SRGB view (the classic mistake) or UNORM\n"
        "L pauses and restarts the light going round the sphere\n"
        "[ and ] lower and raise the forced level of detail of mode 3 by 0.5\n"
        "mode 0 lit, 1 world normal, 2 the map as stored, 3 |n| at the forced level";
      hooks.onReload = [&](const std::vector<std::string>&) { reloadProgram(dev, prog); };
      hooks.onFrame  = [&](Params& p) {
        if (first) status(p);
        if (!first && !paused) angle += LIGHT_SPEED * (p.time - lastTime);
        first    = false;
        lastTime = p.time;
        p.light  = glm::vec4(lightAt(angle), 1.0f);
      };
      hooks.onKey = [&](int key, Params& p) {
        switch (key) {
          case GLFW_KEY_C:             dx = !dx;     bindNormals(dx, srgb); break;
          case GLFW_KEY_G:             srgb = !srgb; bindNormals(dx, srgb); break;
          case GLFW_KEY_K:             p.knob = p.knob == 1 ? 0 : 1; break;
          case GLFW_KEY_L:             paused = !paused; break;
          case GLFW_KEY_LEFT_BRACKET:  p.knobf = std::max(0.0f, p.knobf - 0.5f); break;
          case GLFW_KEY_RIGHT_BRACKET: p.knobf = std::min(maxLod, p.knobf + 0.5f); break;
          default:                     return;
        }
        status(p);
      };

      runWindow(dev, win, target, uniform, params, args, hooks);
      destroySwapchain(dev, win);
    }

    destroyProgram(dev, prog);
    destroyMesh(dev, sphere);
    vkDestroyPipelineLayout(dev.device, layout, nullptr);
    destroyDescriptors(dev, descs);
    destroyBuffer(dev, uniform);
    destroySamplers(dev, samplers);
    destroyTexture(dev, albedo);
    destroyTexture(dev, dxTex);
    destroyTexture(dev, glTex);
    destroyPass(dev, pass);
    destroyTarget(dev, target);
    destroyDevice(dev);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
