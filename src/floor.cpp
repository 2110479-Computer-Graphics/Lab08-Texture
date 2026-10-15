#include "core/Frame.h"
#include "core/Geometry.h"
#include "core/Sampler.h"
#include "core/Shader.h"
#include "core/Timer.h"
#include "utils/Hash.h"
#include "utils/image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

static constexpr std::uint32_t TEX        = 1024;
static constexpr std::uint32_t TILES      = 8;
static constexpr std::uint32_t TILE       = TEX / TILES;
static constexpr std::uint32_t GROUT      = 3;
static constexpr double        PLANE_SIZE = 400.0;
static constexpr double        REPEAT     = 2.0;
static constexpr double        EYE_HEIGHT = 1.0;
static constexpr double        PITCH_DEG  = 8.0;
static constexpr double        FOV_DEG    = 60.0;
static constexpr float         NEAR       = 0.05f;
static constexpr float         FAR        = 1000.0f;
static const glm::vec4         SKY(0.62f, 0.71f, 0.82f, 1.0f);

static constexpr double PI = 3.14159265358979323846;

static constexpr std::uint32_t            PROBE_X    = 600;
static const std::array<std::uint32_t, 6> PROBE_ROWS = { 580, 500, 420, 360, 310, 270 };
static constexpr double                   LOD_TOL    = 0.05;
static constexpr double                   RATIO_TOL  = 0.03;
static constexpr double                   SHARE_TOL  = 0.5;

static constexpr std::uint32_t STRESS_TAPS  = 16;
static constexpr std::uint32_t SWEEP_ROUNDS = 3;
static constexpr std::uint32_t SWEEP_FRAMES = 15;
static constexpr std::uint32_t SWEEP_COPIES = 40;

static std::uint8_t level(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

static Image makeFloorTexture() {
  static const int LIGHT[3]  = { 212, 204, 188 };
  static const int DARK[3]   = {  92,  88,  84 };
  static const int MORTAR[3] = {  44,  40,  38 };

  Image image(TEX, TEX);
  for (std::uint32_t y = 0; y < TEX; ++y)
    for (std::uint32_t x = 0; x < TEX; ++x) {
      std::uint32_t tx    = x / TILE;
      std::uint32_t ty    = y / TILE;
      bool          grout = x % TILE < GROUT || y % TILE < GROUT;
      bool          light = (tx + ty) % 2 == 0;
      int           shade = static_cast<int>(hash01(glm::uvec3(tx, ty, 11u)) * 17.0f) - 8;
      int           noise = static_cast<int>(pcg(glm::uvec2(x, y)) % 13u) - 6;

      std::uint8_t* t = image.at(x, y);
      for (std::uint32_t c = 0; c < 3; ++c) {
        int base = grout ? MORTAR[c] : (light ? LIGHT[c] : DARK[c]) + shade;
        t[c] = level(base + noise);
      }
      t[3] = 255;
    }
  return image;
}

struct FloorCamera {
  glm::dvec3 eye;
  glm::dvec3 forward;
  glm::dvec3 right;
  glm::dvec3 up;
  double     tanHalf = 0.0;
  double     aspect  = 0.0;
};

static FloorCamera floorCamera() {
  double      pitch = PITCH_DEG * PI / 180.0;
  FloorCamera cam{};
  cam.eye     = glm::dvec3(0.0, EYE_HEIGHT, 0.0);
  cam.forward = glm::dvec3(0.0, -std::sin(pitch), -std::cos(pitch));
  cam.right   = glm::normalize(glm::cross(cam.forward, glm::dvec3(0.0, 1.0, 0.0)));
  cam.up      = glm::cross(cam.right, cam.forward);
  cam.tanHalf = std::tan(FOV_DEG * PI / 360.0);
  cam.aspect  = static_cast<double>(WIDTH) / static_cast<double>(HEIGHT);
  return cam;
}

static bool texelAt(const FloorCamera& cam, double px, double py, glm::dvec2& texel,
                    glm::dvec3* hit = nullptr)
{
  double     nx  = 2.0 * px / WIDTH - 1.0;
  double     ny  = 2.0 * py / HEIGHT - 1.0;
  glm::dvec3 dir = cam.forward + cam.right * (nx * cam.tanHalf * cam.aspect) -
                   cam.up * (ny * cam.tanHalf);
  if (dir.y >= 0.0) return false;
  double     t = -cam.eye.y / dir.y;
  glm::dvec3 p = cam.eye + dir * t;
  if (hit != nullptr) *hit = p;
  glm::dvec2 uv = glm::dvec2(p.x, p.z) / REPEAT;
  texel = uv * static_cast<double>(TEX);
  return true;
}

struct Footprint {
  bool   floor  = false;
  double lod    = 0.0;
  double ratio  = 1.0;
  double metres = 0.0;
};

static Footprint footprint(const FloorCamera& cam, std::uint32_t x, std::uint32_t y) {
  Footprint  f{};
  glm::dvec2 centre, left, right, top, bottom;
  glm::dvec3 hit;
  double     cx = x + 0.5;
  double     cy = y + 0.5;
  double     qx = (x & ~1u) + 0.5;
  double     qy = (y & ~1u) + 0.5;
  if (!texelAt(cam, cx, cy, centre, &hit) || !texelAt(cam, qx, cy, left) ||
      !texelAt(cam, qx + 1.0, cy, right) || !texelAt(cam, cx, qy, top) ||
      !texelAt(cam, cx, qy + 1.0, bottom))
    return f;
  double half = PLANE_SIZE / 2.0;
  f.floor  = std::fabs(hit.x) <= half && std::fabs(hit.z) <= half;
  f.metres = std::sqrt(hit.x * hit.x + hit.z * hit.z);

  double lx = glm::length(right - left);
  double ly = glm::length(bottom - top);
  f.lod     = std::log2(std::max(lx, ly));
  f.ratio   = std::clamp(std::max(lx, ly) / std::max(std::min(lx, ly), 1e-12), 1.0, 16.0);
  return f;
}

static const char* verdict(bool pass) { return pass ? "PASS" : "FAIL"; }

static double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return values.empty() ? 0.0 : values[values.size() / 2];
}

int main(int argc, char** argv) {
  try {
    Args args = parseArgs(argc, argv);

    Filter filter = Filter::Trilinear;
    if (!args.filter.empty() &&
        (!filterFromName(args.filter, filter) || filter == Filter::NearestMips))
      throw std::runtime_error("--filter takes nearest, bilinear, trilinear, aniso4 or aniso16, not " +
                               args.filter);

    GLFWwindow* handle =
      args.headless ? nullptr : createHiddenWindow("Lab 08 - Part III", WIDTH, HEIGHT);

    Device dev    = createDevice(handle);
    Target target = createTarget(dev, WIDTH, HEIGHT);
    Pass   pass   = createTargetPass(dev, target, Load::Clear, SKY);

    Texture probeTex  = createRenderTexture(dev, WIDTH, HEIGHT, VK_FORMAT_R32G32B32A32_SFLOAT);
    Pass    probePass = createPass(dev, { { &probeTex, Load::Clear, glm::vec4(0.0f) } });

    Texture  floorTex = createTexture(dev, boxMips(makeFloorTexture()), VK_FORMAT_R8G8B8A8_SRGB);
    Samplers samplers = createSamplers(dev);

    Buffer uniform = createBuffer(dev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Descriptors      descs  = createDescriptors(dev, uniform, 2);
    VkPipelineLayout layout = createPipelineLayout(dev, descs);
    bindTexture(dev, descs.sets[0], 0, floorTex.view, samplers.get(filter, Wrap::Repeat));
    bindTexture(dev, descs.sets[1], 0, floorTex.view, samplers.get(Filter::Trilinear, Wrap::Repeat));

    Program floorProg  = createProgram(dev, pass, layout, "floor.vert", "miplevel.frag", meshLayout());
    Program yoursProbe = createProgram(dev, probePass, layout, "floor.vert", "miplevel.frag",
                                       meshLayout());
    Program hwProbe{};
    bool    hasHw = true;
    try {
      hwProbe = createProgram(dev, probePass, layout, "floor.vert", "floor_querylod.frag",
                              meshLayout());
    } catch (const std::exception&) {
      hasHw = false;
    }

    Mesh floorQuad = plane(static_cast<float>(PLANE_SIZE), static_cast<float>(PLANE_SIZE / REPEAT));
    for (Vertex& v : floorQuad.vertices) v.uv -= glm::vec2(static_cast<float>(PLANE_SIZE / REPEAT / 2.0));
    GpuMesh floorMesh = uploadMesh(dev, floorQuad);

    double      pitch = PITCH_DEG * PI / 180.0;
    glm::vec3   eye(0.0f, static_cast<float>(EYE_HEIGHT), 0.0f);
    glm::vec3   ahead(0.0f, 0.0f, static_cast<float>(-EYE_HEIGHT / std::tan(pitch)));
    OrbitCamera cam = OrbitCamera::looking(eye, ahead);

    Params params{};
    params.resolution = glm::vec2(WIDTH, HEIGHT);
    params.time       = args.time;
    params.mode       = args.mode;
    params.knob       = args.knobSet ? std::max(args.knob, 1u) : 1u;
    params.knobf      = args.knobf;
    params.texSize    = glm::vec2(TEX, TEX);
    params.eye        = glm::vec4(eye, 1.0f);
    params.view       = glm::lookAt(eye, ahead, glm::vec3(0.0f, 1.0f, 0.0f));
    params.proj       = perspectiveReversedZ(static_cast<float>(FOV_DEG * PI / 180.0),
                                             static_cast<float>(WIDTH) / HEIGHT, NEAR, FAR);

    auto drawFloor = [&](VkCommandBuffer cmd, const Program& prog, VkDescriptorSet set,
                         const Pass& into) {
      beginPass(cmd, into);
      bindProgram(cmd, prog, set);
      drawMesh(cmd, floorMesh);
      endPass(cmd);
    };
    auto record = [&](VkCommandBuffer cmd, const Params&) {
      drawFloor(cmd, floorProg, descs.sets[0], pass);
    };
    auto probe = [&](const Program& prog, VkDescriptorSet set) {
      Params p = params;
      p.mode   = 3;
      p.knob   = 1;
      uploadBuffer(uniform, &p, sizeof(Params));
      submitNow(dev, [&](VkCommandBuffer cmd) { drawFloor(cmd, prog, set, probePass); });
      return readTextureFloats(dev, probeTex);
    };

    std::vector<float> yours = probe(yoursProbe, descs.sets[0]);
    std::vector<float> hw;
    if (hasHw) hw = probe(hwProbe, descs.sets[1]);

    std::vector<std::uint8_t> rgba = renderOnce(dev, target, uniform, params, record);
    Timing timing = timePass(dev, WARMUP_FRAMES, MEASURED_FRAMES,
                             [&](VkCommandBuffer cmd) { record(cmd, params); });

    Params refView = params;
    refView.mode   = 0;
    refView.knob   = 1;
    bindTexture(dev, descs.sets[0], 0, floorTex.view, samplers.get(Filter::Aniso16, Wrap::Repeat));
    std::vector<std::uint8_t> refImage = renderOnce(dev, target, uniform, refView, record);
    double diff = diffFromReference("floor", refImage, target.width, target.height);

    Params sweep = params;
    sweep.mode   = 0;
    sweep.knob   = STRESS_TAPS;
    uploadBuffer(uniform, &sweep, sizeof(Params));
    auto timed = [&](VkCommandBuffer cmd) { record(cmd, sweep); };
    timePass(dev, SWEEP_WARMUP, 1, timed, SWEEP_COPIES);
    std::array<std::array<double, SWEEP_ROUNDS>, 5> rounds{};
    for (std::uint32_t r = 0; r < SWEEP_ROUNDS; ++r)
      for (std::size_t i = 0; i < 5; ++i) {
        bindTexture(dev, descs.sets[0], 0, floorTex.view, samplers.get(SWEEP_FILTERS[i], Wrap::Repeat));
        rounds[i][r] = timePass(dev, 3, SWEEP_FRAMES, timed, SWEEP_COPIES).medianMs;
      }
    bindTexture(dev, descs.sets[0], 0, floorTex.view, samplers.get(filter, Wrap::Repeat));

    FloorCamera fc      = floorCamera();
    double      horizon = HEIGHT / 2.0 - HEIGHT / 2.0 * std::tan(pitch) / fc.tanHalf;

    std::cout << std::fixed << std::setprecision(0);
    std::cout << label("shaders") << "floor.vert + miplevel.frag (with floor_helpers.glsl, color.glsl)\n";
    std::cout << label("texture") << TEX << "x" << TEX << " SRGB checker, " << floorTex.levels
              << " levels box-filtered on the CPU, one repeat every " << REPEAT << " m\n";
    std::cout << label("camera") << EYE_HEIGHT << " m above a " << PLANE_SIZE << " m floor, "
              << PITCH_DEG << " deg down, " << FOV_DEG << " deg fov, horizon at row " << horizon
              << "\n";
    std::cout << label("filter") << filterName(filter);
    float aniso = samplers.anisotropy[static_cast<std::uint32_t>(filter)];
    if (aniso > 1.0f) std::cout << " (max anisotropy " << aniso << ")";
    std::cout << ", " << params.knob << (params.knob == 1 ? " texture() call" : " texture() calls")
              << " a pixel\n";

    std::cout << label("probes") << "column x=" << PROBE_X << ", near to far. cpu: exact, from where"
              << " the rays through\n"
              << indent() << "the pixel and its neighbours meet the floor. yours: mode 3 of"
              << " miplevel.frag.\n"
              << indent() << "hw: textureQueryLod with a trilinear sampler (the raw lod).\n";

    std::cout << std::setprecision(2);
    double hwWorst = 0.0;
    for (std::uint32_t y : PROBE_ROWS) {
      Footprint   cpu = footprint(fc, PROBE_X, y);
      std::size_t i   = (std::size_t(y) * WIDTH + PROBE_X) * 4;
      double      lod = yours[i] * 16.0;
      double      rat = yours[i + 1] * 16.0;
      bool ok = std::isfinite(lod) && std::isfinite(rat) && std::fabs(lod - cpu.lod) <= LOD_TOL &&
                std::fabs(rat - cpu.ratio) <= RATIO_TOL * cpu.ratio;

      std::ostringstream name;
      name << "row y=" << y;
      std::cout << label(name.str()) << "cpu lod " << std::setw(5) << cpu.lod << " ratio "
                << std::setw(5) << cpu.ratio << "   yours " << std::setw(5) << lod << " "
                << std::setw(5) << rat << "   hw ";
      if (hasHw) {
        std::cout << std::setw(5) << hw[i];
        hwWorst = std::max(hwWorst, std::fabs(hw[i] - cpu.lod));
      } else {
        std::cout << "  n/a";
      }
      std::cout << "   " << verdict(ok) << "  " << std::setprecision(1) << std::setw(5)
                << cpu.metres << " m away\n" << std::setprecision(2);
    }
    std::cout << label("tolerance") << "lod within " << LOD_TOL << " levels, ratio within "
              << std::setprecision(0) << RATIO_TOL * 100.0 << "%" << std::setprecision(2);
    if (hasHw) std::cout << ";  hw and cpu at most " << hwWorst << " levels apart";
    std::cout << "\n";

    std::size_t gpuFloor = 0, gpuOver = 0, cpuFloor = 0, cpuOver = 0;
    for (std::uint32_t y = 0; y < HEIGHT; ++y)
      for (std::uint32_t x = 0; x < WIDTH; ++x) {
        std::size_t i = (std::size_t(y) * WIDTH + x) * 4;
        if (yours[i + 3] > 0.5f) {
          ++gpuFloor;
          gpuOver += yours[i + 1] * 16.0f > 4.0f ? 1 : 0;
        }
        Footprint f = footprint(fc, x, y);
        if (f.floor) {
          ++cpuFloor;
          cpuOver += f.ratio > 4.0 ? 1 : 0;
        }
      }
    auto share = [](std::size_t part, std::size_t whole) {
      return whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
    };
    double gpuShare = share(gpuOver, gpuFloor);
    double cpuShare = share(cpuOver, cpuFloor);
    bool   shareOk  = std::fabs(gpuShare - cpuShare) <= SHARE_TOL;

    auto crossing = [&](std::uint32_t x, double& metres) {
      for (std::uint32_t y = HEIGHT - 1; y > 0; --y) {
        Footprint f = footprint(fc, x, y);
        if (f.floor && f.ratio > 4.0) {
          metres = f.metres;
          return y;
        }
      }
      return 0u;
    };
    double        midMetres = 0.0, edgeMetres = 0.0;
    std::uint32_t midRow    = crossing(WIDTH / 2, midMetres);
    std::uint32_t edgeRow   = crossing(0, edgeMetres);

    std::cout << std::setprecision(1);
    std::cout << label("ratio > 4") << "yours on " << gpuShare << "% of the floor pixels, cpu "
              << cpuShare << "%   " << verdict(shareOk) << "\n"
              << indent() << "cpu: from row " << midRow << " up in the middle column (" << midMetres
              << " m away),\n"
              << indent() << "from row " << edgeRow << " up at the left edge (" << edgeMetres
              << " m away)\n";

    double trilinear = median({ rounds[2].begin(), rounds[2].end() });
    std::cout << std::setprecision(3);
    std::cout << label("sweep") << "GPU ms a frame, " << STRESS_TAPS << " texture() calls a pixel, "
              << SWEEP_ROUNDS << " rounds\n";
    std::cout << indent() << "filter         median   each round               vs trilinear\n";
    for (std::size_t i = 0; i < 5; ++i) {
      double m = median({ rounds[i].begin(), rounds[i].end() });
      std::cout << indent() << std::left << std::setw(13) << filterName(SWEEP_FILTERS[i])
                << std::right << std::setw(7) << m << "   ";
      for (double r : rounds[i]) std::cout << std::setw(6) << r << " ";
      std::cout << "    " << std::setprecision(2) << std::setw(6)
                << (trilinear > 0.0 ? m / trilinear : 0.0) << "x\n" << std::setprecision(3);
    }
    std::cout << indent() << "nearest and bilinear have no mips: they read level 0 at every distance\n";

    std::cout << label("image") << imageResult(diff) << "   (mode 0, aniso16, 1 texture() call)\n";

    if (shaderHasMarker("miplevel.frag", "TODO(TASK 3a)"))
      std::cout << label("incomplete") << "miplevel.frag still has TODO(TASK 3a): lod_of returns 0,\n"
                << indent() << "so every pixel claims level 0 and mode 1 is one colour\n";
    if (shaderHasMarker("miplevel.frag", "TODO(TASK 3b)"))
      std::cout << label("incomplete") << "miplevel.frag still has TODO(TASK 3b): aniso_ratio returns"
                << " 1,\n"
                << indent() << "so every footprint looks square and mode 2 is one colour\n";

    std::cout << label("gpu time") << timing.medianMs << " ms median, " << timing.p95Ms
              << " ms p95" << std::endl;

    if (!args.save.empty()) writePng(args.save, rgba, target.width, target.height);

    if (!args.headless) {
      Window win = createSwapchain(dev, handle);

      FrameHooks hooks{};
      hooks.record    = record;
      hooks.shaders   = { "floor.vert", "miplevel.frag", "floor_querylod.frag" };
      hooks.savePath  = std::string(REPORT_DIR) + "/floor.png";
      hooks.camera    = &cam;
      hooks.extraKeys = "mode 0 textured, 1 mip level bands, 2 anisotropy heat map, 3 probe encoding\n"
                        "F cycles the filter: nearest, bilinear, trilinear, aniso4, aniso16\n"
                        "T switches between 1 and 16 texture() calls a pixel";
      hooks.onReload  = [&](const std::vector<std::string>&) {
        reloadProgram(dev, floorProg);
        reloadProgram(dev, yoursProbe);
        if (hasHw) reloadProgram(dev, hwProbe);
      };
      hooks.onKey = [&](int key, Params& live) {
        if (key == GLFW_KEY_F) {
          std::size_t i = 0;
          while (i < 5 && SWEEP_FILTERS[i] != filter) ++i;
          filter = SWEEP_FILTERS[(i + 1) % 5];
          bindTexture(dev, descs.sets[0], 0, floorTex.view, samplers.get(filter, Wrap::Repeat));
          std::cout << label("filter") << filterName(filter) << std::endl;
        } else if (key == GLFW_KEY_T) {
          live.knob = live.knob == 1 ? STRESS_TAPS : 1;
          std::cout << label("taps") << live.knob << (live.knob == 1 ? " texture() call" : " texture() calls")
                    << " a pixel" << std::endl;
        }
      };

      runWindow(dev, win, target, uniform, params, args, hooks);
      destroySwapchain(dev, win);
    }

    destroyMesh(dev, floorMesh);
    if (hasHw) destroyProgram(dev, hwProbe);
    destroyProgram(dev, yoursProbe);
    destroyProgram(dev, floorProg);
    vkDestroyPipelineLayout(dev.device, layout, nullptr);
    destroyDescriptors(dev, descs);
    destroyBuffer(dev, uniform);
    destroySamplers(dev, samplers);
    destroyTexture(dev, floorTex);
    destroyPass(dev, probePass);
    destroyTexture(dev, probeTex);
    destroyPass(dev, pass);
    destroyTarget(dev, target);
    destroyDevice(dev);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
