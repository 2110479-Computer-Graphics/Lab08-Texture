#include "core/Frame.h"
#include "core/Geometry.h"
#include "core/Sampler.h"
#include "core/Shader.h"
#include "core/Timer.h"
#include "utils/Color.h"
#include "utils/Hash.h"
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
#include <stdexcept>
#include <string>

static constexpr double PI = 3.14159265358979323846;

static constexpr std::uint32_t TEX      = 256;
static constexpr std::uint32_t TILE     = 256;
static constexpr std::uint32_t TILE_Y   = 172;
static const std::array<std::uint32_t, 3> TILE_X = { 8, 272, 536 };
enum TileIndex : std::uint32_t { HARDWARE = 0, YOURS = 1, NO_DECODE = 2 };

static constexpr std::uint32_t RAMP_ROW   = 64;
static constexpr std::uint32_t ALBEDO_ROW = 128;
static constexpr std::uint32_t MID_GREY   = 128;

static constexpr std::uint32_t BRICK_W = 32;
static constexpr std::uint32_t BRICK_H = 16;
static constexpr std::uint32_t MORTAR  = 2;

static const std::array<glm::uvec2, 3> CHARCOAL = {{ { 1, 2 }, { 4, 6 }, { 6, 1 } }};
static const std::array<glm::uvec2, 2> WHITE    = {{ { 2, 5 }, { 5, 0 } }};
static const glm::uvec2                RED      = { 3, 3 };

static constexpr float WALL_W = 8.0f;
static constexpr float WALL_H = 5.0f;
static const glm::vec2 TILES  = { 4.0f, 2.5f };
static const glm::vec3 EYE    = { 0.5f, 0.724f, 5.149f };
static const glm::vec3 LOOK   = { 0.5f, 0.0f, 0.0f };
static constexpr float FOV_Y  = 45.0f;
static constexpr float SPLIT  = 400.0f;

static const glm::dvec3 WARM      = { 1.0, 0.62, 0.32 };
static const glm::dvec3 COOL      = { 0.32, 0.58, 1.0 };
static constexpr double WARM_PEAK = 1.0;
static constexpr double COOL_PEAK = 1.2;
static constexpr double THROW     = 6.0;
static constexpr double INNER_DEG = 4.5;
static constexpr double OUTER_DEG = 10.2;
static const glm::vec2  AIM_A     = { 0.5f, 0.55f };
static const glm::vec2  AIM_B     = { 0.5f, -0.55f };

static constexpr std::uint32_t EDGE       = 3;
static constexpr std::uint32_t PROBE_X    = 390;
static constexpr int           SEARCH     = 12;
static constexpr int           PROBE_TOL  = 2;
static constexpr int           SAME_TOL   = 4;

static const std::array<const char*, 4> WALL_MODES = { "gamma-space lighting", "albedo_check",
                                                       "no decode", "no encode" };
static const std::array<const char*, 4> TILE_MODES = { "shown", "albedo_check", "linear values raw",
                                                       "|A - B| x 64 on tile B" };

static bool listed(const glm::uvec2* first, const glm::uvec2* last, glm::uvec2 brick) {
  return std::find(first, last, brick) != last;
}

static bool charcoal(glm::uvec2 brick) {
  return listed(CHARCOAL.data(), CHARCOAL.data() + CHARCOAL.size(), brick);
}

static bool white(glm::uvec2 brick) { return listed(WHITE.data(), WHITE.data() + WHITE.size(), brick); }

static std::uint8_t level(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

static glm::uvec2 brickOf(std::uint32_t x, std::uint32_t ly) {
  std::uint32_t course = ly / BRICK_H;
  std::uint32_t sx     = (x + (course % 2) * (BRICK_W / 2)) % TEX;
  return { course, sx / BRICK_W };
}

static glm::uvec2 inBrick(std::uint32_t x, std::uint32_t ly) {
  std::uint32_t course = ly / BRICK_H;
  std::uint32_t sx     = (x + (course % 2) * (BRICK_W / 2)) % TEX;
  return { sx % BRICK_W, ly % BRICK_H };
}

static int clayLevel(glm::uvec2 brick, std::uint32_t c) {
  float h = hash01(glm::uvec3(brick.x, brick.y, 7u + c));
  int   base[3] = { 140, 78, 62 };
  int   span[3] = { 40, 22, 16 };
  return base[c] + static_cast<int>(h * static_cast<float>(span[c]));
}

static void paintBricks(Image& image, std::uint32_t firstRow) {
  for (std::uint32_t y = firstRow; y < image.height; ++y)
    for (std::uint32_t x = 0; x < image.width; ++x) {
      std::uint32_t ly     = y - firstRow;
      glm::uvec2    brick  = brickOf(x, ly);
      glm::uvec2    at     = inBrick(x, ly);
      bool          mortar = at.y < MORTAR || at.x < MORTAR;

      std::uint8_t* t = image.at(x, y);
      t[3] = 255;
      for (std::uint32_t c = 0; c < 3; ++c) {
        int noise = static_cast<int>(pcg(glm::uvec3(x, y, c)) % 7u) - 3;
        int value = 0;
        if (mortar)
          value = (c == 0 ? 168 : c == 1 ? 162 : 152) + noise;
        else if (charcoal(brick))
          value = 14 + noise;
        else if (white(brick))
          value = 251 + noise;
        else if (brick == RED)
          value = c == 0 ? 200 : c == 1 ? 30 : 25;
        else
          value = clayLevel(brick, c) + noise;
        t[c] = level(value);
      }
    }
}

static Image makeTexture() {
  Image image(TEX, TEX);

  for (std::uint32_t y = 0; y < ALBEDO_ROW; ++y)
    for (std::uint32_t x = 0; x < TEX; ++x) {
      std::uint8_t* t = image.at(x, y);
      t[0] = t[1] = t[2] = static_cast<std::uint8_t>(x);
      t[3] = 255;
    }

  paintBricks(image, ALBEDO_ROW);
  return image;
}

static Image makeBricks() {
  Image image(TEX, TEX);
  paintBricks(image, 0);
  return image;
}

static std::uint32_t texelFor(std::uint32_t p, std::uint32_t size) {
  double u = (static_cast<double>(p) + 0.5) / TILE;
  return std::min(static_cast<std::uint32_t>(u * size), size - 1);
}

struct Range {
  std::size_t pixels = 0;
  std::size_t dark   = 0;
  std::size_t bright = 0;

  double darkPct() const { return pixels ? 100.0 * static_cast<double>(dark) / static_cast<double>(pixels) : 0.0; }
  double brightPct() const { return pixels ? 100.0 * static_cast<double>(bright) / static_cast<double>(pixels) : 0.0; }
};

static Range cpuRange(const Image& image, std::uint32_t firstRow, bool perChannel) {
  Range range{};
  for (std::uint32_t py = firstRow; py < TILE; ++py)
    for (std::uint32_t px = 0; px < TILE; ++px) {
      const std::uint8_t* t = image.at(texelFor(px, image.width), texelFor(py, image.height));
      double lin[3] = { byteToLinear(t[0]), byteToLinear(t[1]), byteToLinear(t[2]) };
      ++range.pixels;
      if (perChannel) {
        bool dark   = lin[0] < 0.04 || lin[1] < 0.04 || lin[2] < 0.04;
        bool bright = lin[0] > 0.9 || lin[1] > 0.9 || lin[2] > 0.9;
        range.dark   += dark ? 1 : 0;
        range.bright += bright ? 1 : 0;
      } else {
        double y = luminance(lin[0], lin[1], lin[2]);
        range.dark   += y < 0.04 ? 1 : 0;
        range.bright += y > 0.9 ? 1 : 0;
      }
    }
  return range;
}

static Range gpuRange(const std::vector<std::uint8_t>& rgba, std::uint32_t firstRow) {
  Range range{};
  for (std::uint32_t py = firstRow; py < TILE; ++py)
    for (std::uint32_t px = 0; px < TILE; ++px) {
      std::vector<std::uint8_t> c = pixelAt(rgba, WIDTH, TILE_X[YOURS] + px, TILE_Y + py);
      ++range.pixels;
      range.dark   += (c[0] == 0 && c[1] == 0 && c[2] == 255) ? 1 : 0;
      range.bright += (c[0] == 255 && c[1] == 0 && c[2] == 0) ? 1 : 0;
    }
  return range;
}

static int tileTexel(const std::vector<std::uint8_t>& rgba, std::uint32_t tile, std::uint32_t x,
                     std::uint32_t y)
{
  return pixelAt(rgba, WIDTH, TILE_X[tile] + x, TILE_Y + y)[0];
}

static const char* verdict(bool pass) { return pass ? "PASS" : "FAIL"; }

static glm::vec4 aimsAt(float time) {
  float     s = time - 1.0f;
  glm::vec2 a = AIM_A + glm::vec2(0.0f, 0.15f * std::sin(0.4f * s));
  glm::vec2 b = AIM_B + glm::vec2(0.0f, 0.15f * std::sin(0.3f * s));
  return glm::vec4(a, b);
}

static double spotShape(glm::dvec2 aim, glm::dvec3 p) {
  glm::dvec3 d     = p - glm::dvec3(aim, THROW);
  double     dist  = glm::length(d);
  double     inner = std::cos(INNER_DEG * PI / 180.0);
  double     outer = std::cos(OUTER_DEG * PI / 180.0);
  double     t     = std::clamp((-d.z / dist - outer) / (inner - outer), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t) * THROW * THROW / (dist * dist);
}

struct View {
  glm::dmat4 viewProj;
  glm::dmat4 inverse;
  glm::dvec3 eye;
};

static glm::dvec2 project(const View& view, glm::dvec3 world) {
  glm::dvec4 clip = view.viewProj * glm::dvec4(world, 1.0);
  glm::dvec2 ndc  = glm::dvec2(clip) / clip.w;
  return glm::dvec2((ndc.x * 0.5 + 0.5) * WIDTH, (ndc.y * 0.5 + 0.5) * HEIGHT);
}

static glm::dvec3 onWall(const View& view, double px, double py) {
  double     nx    = px / WIDTH * 2.0 - 1.0;
  double     ny    = py / HEIGHT * 2.0 - 1.0;
  glm::dvec4 point = view.inverse * glm::dvec4(nx, ny, 0.5, 1.0);
  glm::dvec3 dir   = glm::dvec3(point) / point.w - view.eye;
  return view.eye + (-view.eye.z / dir.z) * dir;
}

static glm::dvec2 wallUV(glm::dvec3 p) {
  return glm::dvec2((p.x / WALL_W + 0.5) * TILES.x, (0.5 - p.y / WALL_H) * TILES.y);
}

static bool clayFace(glm::dvec2 uv) {
  double        fu    = uv.x - std::floor(uv.x);
  double        fv    = uv.y - std::floor(uv.y);
  std::uint32_t tx    = std::min(static_cast<std::uint32_t>(fu * TEX), TEX - 1);
  std::uint32_t ly    = std::min(static_cast<std::uint32_t>(fv * TEX), TEX - 1);
  glm::uvec2    brick = brickOf(tx, ly);
  glm::uvec2    at    = inBrick(tx, ly);
  bool face = at.x >= MORTAR + EDGE && at.x < BRICK_W - EDGE && at.y >= MORTAR + EDGE &&
              at.y < BRICK_H - EDGE;
  return face && !charcoal(brick) && !white(brick) && brick != RED;
}

static glm::uvec2 findProbe(const View& view, glm::dvec2 target, int away) {
  int        cy   = static_cast<int>(std::floor(project(view, glm::dvec3(target, 0.0)).y));
  glm::uvec2 best(PROBE_X, static_cast<std::uint32_t>(std::clamp(cy, 0, static_cast<int>(HEIGHT) - 1)));
  for (int dy = 0; dy <= SEARCH; ++dy)
    for (int y : { cy - dy, cy + dy })
      if ((away == 0 || (y - cy) * away >= 0) && y >= 0 && y < static_cast<int>(HEIGHT) && clayFace(wallUV(onWall(view, PROBE_X + 0.5, y + 0.5))))
        return glm::uvec2(PROBE_X, static_cast<std::uint32_t>(y));
  return best;
}

static std::vector<int> bytesAt(const std::vector<std::uint8_t>& rgba, glm::uvec2 p) {
  std::vector<std::uint8_t> c = pixelAt(rgba, WIDTH, p.x, p.y);
  return { c[0], c[1], c[2] };
}

static std::string rgb(const std::vector<int>& c) {
  std::ostringstream text;
  text << "(" << std::setw(3) << c[0] << "," << std::setw(4) << c[1] << "," << std::setw(4) << c[2] << ")";
  return text.str();
}

static std::string triple(glm::dvec3 v) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(2) << "(" << v.x << ", " << v.y << ", " << v.z << ")";
  return text.str();
}

static std::string num(double v) {
  std::ostringstream text;
  text << v;
  return text.str();
}

static std::string pair(glm::dvec2 v) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(2) << "(" << v.x << ", " << v.y << ")";
  return text.str();
}

static float splitOption(const Args& args) {
  auto it = args.options.find("split");
  if (it == args.options.end()) return SPLIT;
  return std::clamp(std::stof(it->second), 0.0f, static_cast<float>(WIDTH));
}

static bool tilesOption(const Args& args) {
  for (const auto& option : args.options)
    if (option.first != "split" && option.first != "view")
      throw std::runtime_error("srgb knows --opt split=x and --opt view=wall|tiles, not " + option.first);
  auto it = args.options.find("view");
  if (it == args.options.end() || it->second == "wall") return false;
  if (it->second == "tiles") return true;
  throw std::runtime_error("--opt view= wants wall or tiles, not " + it->second);
}

int main(int argc, char** argv) {
  try {
    Args  args       = parseArgs(argc, argv);
    float split      = splitOption(args);
    bool  tilesFirst = tilesOption(args);

    GLFWwindow* handle =
      args.headless ? nullptr : createHiddenWindow("Lab 08 - Part I", WIDTH, HEIGHT);

    Device dev    = createDevice(handle);
    Target target = createTarget(dev, WIDTH, HEIGHT);
    Pass   pass   = createTargetPass(dev, target);

    bool    own     = !args.albedo.empty();
    Image   albedo  = own ? loadPng(args.albedo) : makeTexture();
    Texture decoded = createTexture(dev, { albedo }, VK_FORMAT_R8G8B8A8_SRGB);
    Texture raw     = createTexture(dev, { albedo }, VK_FORMAT_R8G8B8A8_UNORM);

    Image              brickTile  = own ? albedo : makeBricks();
    std::vector<Image> brickChain = boxMips(brickTile);
    Texture            bricks     = createTexture(dev, brickChain, VK_FORMAT_R8G8B8A8_SRGB);

    Samplers  samplers  = createSamplers(dev);
    VkSampler nearest   = samplers.get(Filter::Nearest, Wrap::Clamp);
    VkSampler trilinear = samplers.get(Filter::Trilinear, Wrap::Repeat);

    Buffer uniform = createBuffer(dev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Descriptors      descs  = createDescriptors(dev, uniform, 2);
    VkPipelineLayout layout = createPipelineLayout(dev, descs);
    bindTexture(dev, descs.sets[0], 0, decoded.view, nearest);
    bindTexture(dev, descs.sets[0], 1, raw.view, nearest);
    bindTexture(dev, descs.sets[1], 0, bricks.srgbView, trilinear);
    bindTexture(dev, descs.sets[1], 1, bricks.unormView, trilinear);

    Program tiles = createProgram(dev, pass, layout, "fullscreen.vert", "srgb_tiles.frag");
    Program wall  = createProgram(dev, pass, layout, "srgb_wall.vert", "srgb_wall.frag", meshLayout());

    Mesh wallMesh = quad(WALL_W, WALL_H);
    for (Vertex& v : wallMesh.vertices) v.uv *= TILES;
    GpuMesh wallGpu = uploadMesh(dev, wallMesh);

    Params params{};
    params.resolution = glm::vec2(WIDTH, HEIGHT);
    params.time       = args.time;
    params.mode       = args.mode;
    params.light      = aimsAt(args.time);
    params.eye        = glm::vec4(EYE, 1.0f);
    params.knob       = 0;
    params.knobf      = split;
    params.texSize    = glm::vec2(brickTile.width, brickTile.height);
    params.view       = glm::lookAt(EYE, LOOK, glm::vec3(0.0f, 1.0f, 0.0f));
    params.proj       = perspectiveReversedZ(glm::radians(FOV_Y),
                                             static_cast<float>(WIDTH) / static_cast<float>(HEIGHT),
                                             0.1f, 100.0f);

    bool showWall = !tilesFirst;

    auto drawTiles = [&](VkCommandBuffer cmd, const Params&) {
      beginPass(cmd, pass);
      bindProgram(cmd, tiles, descs.sets[0]);
      drawFullscreen(cmd);
      endPass(cmd);
    };
    auto drawWall = [&](VkCommandBuffer cmd, const Params&) {
      beginPass(cmd, pass);
      bindProgram(cmd, wall, descs.sets[1]);
      drawMesh(cmd, wallGpu);
      endPass(cmd);
    };
    auto record = [&](VkCommandBuffer cmd, const Params& p) {
      if (showWall) drawWall(cmd, p);
      else drawTiles(cmd, p);
    };

    Params view = params;
    view.mode   = 0;
    std::vector<std::uint8_t> shown = renderOnce(dev, target, uniform, view, drawTiles);
    view.mode   = 1;
    std::vector<std::uint8_t> flags = renderOnce(dev, target, uniform, view, drawTiles);
    view.mode   = 2;
    std::vector<std::uint8_t> linear = renderOnce(dev, target, uniform, view, drawTiles);

    Params lit = params;
    lit.mode   = 0;
    lit.knobf  = static_cast<float>(WIDTH);
    std::vector<std::uint8_t> correct = renderOnce(dev, target, uniform, lit, drawWall);
    lit.knobf  = 0.0f;
    std::vector<std::uint8_t> gamma = renderOnce(dev, target, uniform, lit, drawWall);
    lit.knob   = 1;
    std::vector<std::uint8_t> stored = renderOnce(dev, target, uniform, lit, drawWall);

    std::vector<std::uint8_t> rgba = renderOnce(dev, target, uniform, params, record);
    Timing timing = timePass(dev, WARMUP_FRAMES, MEASURED_FRAMES,
                             [&](VkCommandBuffer cmd) { record(cmd, params); });

    std::cout << std::fixed << std::setprecision(4);
    std::cout << label("shaders") << "fullscreen.vert + srgb_tiles.frag for the tiles,\n"
              << indent() << "srgb_wall.vert + srgb_wall.frag for the wall, both include color.glsl\n";
    std::cout << label("texture") << albedo.width << "x" << albedo.height
              << (own ? " from " + args.albedo : std::string(" generated"))
              << ", the same bytes twice: tex0 SRGB, tex1 UNORM\n";
    std::cout << label("tiles") << "A x " << TILE_X[0] << ": hardware decode   B x " << TILE_X[1]
              << ": your decode   C x " << TILE_X[2] << ": no decode\n";

    bool decodeOk = false, rampOk = false, roundOk = false, noDecodeOk = false;
    if (own) {
      std::cout << label("decode") << "n/a: --albedo replaced the grey ramp the decode checks use\n";
    } else {
      double cpuLinear = srgbToLinear(MID_GREY / 255.0);
      int    cpuByte   = toByte(cpuLinear);
      int    yours     = tileTexel(linear, YOURS, MID_GREY, RAMP_ROW);
      int    hardware  = tileTexel(linear, HARDWARE, MID_GREY, RAMP_ROW);
      decodeOk = yours == cpuByte;
      std::cout << label("decode") << "texel " << MID_GREY << " -> linear " << cpuLinear << " (byte "
                << cpuByte << ")   yours " << yours << "   hardware " << hardware << "   "
                << verdict(decodeOk) << "\n";

      int worst = 0, worstAt = 0;
      for (std::uint32_t x = 0; x < TEX; ++x) {
        int d = std::abs(tileTexel(linear, YOURS, x, RAMP_ROW) - tileTexel(linear, HARDWARE, x, RAMP_ROW));
        if (d > worst) { worst = d; worstAt = static_cast<int>(x); }
      }
      rampOk = worst <= 1;
      std::cout << label("ramp") << "max |yours - hardware| over 256 levels  " << worst;
      if (worst > 0) std::cout << " (texel " << worstAt << ")";
      std::cout << "   " << verdict(rampOk) << " (<= 1)\n";

      int roundYours = 0, roundHardware = 0;
      for (std::uint32_t x = 0; x < TEX; ++x) {
        roundYours    = std::max(roundYours, std::abs(tileTexel(shown, YOURS, x, RAMP_ROW) - static_cast<int>(x)));
        roundHardware = std::max(roundHardware, std::abs(tileTexel(shown, HARDWARE, x, RAMP_ROW) - static_cast<int>(x)));
      }
      roundOk = roundYours == 0 && roundHardware == 0;
      std::cout << label("roundtrip") << "max |encode(decode(x)) - x|  " << roundYours
                << " with your decode, " << roundHardware << " with the hardware's   "
                << verdict(roundOk) << "\n";

      int cpuShown = toByte(linearToSrgb(MID_GREY / 255.0));
      int shownC   = tileTexel(shown, NO_DECODE, MID_GREY, RAMP_ROW);
      noDecodeOk = shownC == cpuShown;
      std::cout << label("no decode") << "texel " << MID_GREY << " displays as " << shownC
                << " (should be " << MID_GREY << "), cpu " << cpuShown << "   "
                << verdict(noDecodeOk) << "\n";
    }

    std::uint32_t firstRow = own ? 0 : ALBEDO_ROW;
    Range gpu        = gpuRange(flags, firstRow);
    Range cpu        = cpuRange(albedo, firstRow, false);
    Range perChannel = cpuRange(albedo, firstRow, true);
    long  slack      = static_cast<long>(cpu.pixels / 1000);
    bool  rangeOk    = std::labs(static_cast<long>(gpu.dark) - static_cast<long>(cpu.dark)) <= slack &&
                       std::labs(static_cast<long>(gpu.bright) - static_cast<long>(cpu.bright)) <= slack;

    std::cout << std::setprecision(1);
    std::cout << label("range") << "too dark " << gpu.darkPct() << "% (cpu " << cpu.darkPct()
              << "%)   too bright " << gpu.brightPct() << "% (cpu " << cpu.brightPct() << "%)   "
              << verdict(rangeOk) << "\n"
              << indent() << "luminance of the linear colour, " << (own ? "all" : "the albedo half")
              << " of tile B\n";
    std::cout << label("per channel") << "the same limits on each channel alone (cpu): too dark "
              << perChannel.darkPct() << "%, too bright " << perChannel.brightPct() << "%\n";

    View cam{};
    cam.viewProj = glm::dmat4(params.proj) * glm::dmat4(params.view);
    cam.inverse  = glm::inverse(cam.viewProj);
    cam.eye      = glm::dvec3(EYE);

    glm::dvec2 aimA(params.light.x, params.light.y);
    glm::dvec2 aimB(params.light.z, params.light.w);
    glm::dvec3 warm = WARM * WARM_PEAK;
    glm::dvec3 cool = COOL * COOL_PEAK;

    struct Probe {
      const char* name;
      glm::uvec2  pixel;
    };
    std::array<Probe, 3> probes = {{ { "spot A alone", findProbe(cam, aimA, -1) },
                                     { "overlap", findProbe(cam, (aimA + aimB) * 0.5, 0) },
                                     { "spot B alone", findProbe(cam, aimB, 1) } }};

    std::cout << std::setprecision(2);
    std::cout << label("wall") << num(WALL_W) << " x " << num(WALL_H) << " m, " << brickTile.width << "x"
              << brickTile.height << (own ? " from " + args.albedo : std::string(" bricks generated"))
              << ", repeated " << num(TILES.x) << " x " << num(TILES.y) << ", camera at "
              << triple(glm::dvec3(EYE)) << "\n"
              << indent() << "one upload, " << brickChain.size()
              << " box mips, trilinear, repeat: tex0 its SRGB view, tex1 its UNORM view\n";
    std::cout << label("spotlights") << "A warm " << triple(WARM) << " x " << WARM_PEAK << " above, B cool "
              << triple(COOL) << " x " << COOL_PEAK << " below\n"
              << indent() << "aimed at " << pair(aimA) << " and " << pair(aimB) << " m from " << num(THROW)
              << " m out, no ambient\n"
              << indent() << "cone " << num(INNER_DEG) << " to " << num(OUTER_DEG)
              << " deg, falloff 1 / d^2, both on the split column\n"
              << indent() << "yours  left of the split: linear_to_srgb(srgb_to_linear(raw) * (A + B))\n"
              << indent() << "gamma  right of it, mode 0: raw * (linear_to_srgb(A) + linear_to_srgb(B)),\n"
              << indent() << "       each light tuned alone in gamma space\n"
              << indent() << "cpu    yours in double from raw, the wall as stored (knob 1)\n"
              << indent() << "split at " << WIDTH << " for yours, at 0 for gamma, time " << num(params.time)
              << "; probes in column " << PROBE_X << (own ? "\n" : ", on clay bricks\n");

    std::ostringstream shapes, same;
    shapes << std::fixed << std::setprecision(2);
    for (std::size_t k = 0; k < probes.size(); ++k) {
      const Probe& probe = probes[k];
      glm::dvec3   p     = onWall(cam, probe.pixel.x + 0.5, probe.pixel.y + 0.5);
      double       sa    = spotShape(aimA, p);
      double       sb    = spotShape(aimB, p);
      glm::dvec3   L     = warm * sa + cool * sb;

      std::vector<int> rawBytes = bytesAt(stored, probe.pixel);
      std::vector<int> yours    = bytesAt(correct, probe.pixel);
      std::vector<int> shownG   = bytesAt(gamma, probe.pixel);
      std::vector<int> expect(3);
      bool ok = true, clipsG = false, clipsY = false;
      int  apart = 0, brighter = 0;
      for (int c = 0; c < 3; ++c) {
        expect[c] = toByte(linearToSrgb(srgbToLinear(rawBytes[c] / 255.0) * L[c]));
        ok        = ok && std::abs(yours[c] - expect[c]) <= PROBE_TOL;
        apart     = std::max(apart, std::abs(shownG[c] - yours[c]));
        brighter  = std::max(brighter, shownG[c] - yours[c]);
        clipsG    = clipsG || shownG[c] == 255;
        clipsY    = clipsY || yours[c] == 255;
      }

      std::ostringstream at;
      at << "(" << probe.pixel.x << ", " << probe.pixel.y << ")";
      std::cout << label(probe.name) << std::left << std::setw(12) << at.str() << std::right << "yours "
                << rgb(yours) << "  cpu " << rgb(expect) << "  " << verdict(ok) << "   gamma " << rgb(shownG)
                << "\n";
      shapes << " (" << sa << ", " << sb << ")";

      same << (k == 0 ? "" : ", ") << (k == 0 ? "A alone " : k == 1 ? "overlap " : "B alone ");
      if (apart <= SAME_TOL) same << "yes (max " << apart << ")";
      else same << "no: gamma up to +" << brighter << (clipsG && !clipsY ? " and clipped" : "");
    }
    std::cout << indent() << "shapes (A, B)" << shapes.str() << ", PASS within " << PROBE_TOL << " levels\n";
    std::cout << label("gamma = yours?") << same.str() << "\n"
              << indent() << "yes = within " << SAME_TOL
              << " levels (sRGB is no pure power: enc(a b) is only nearly enc(a) enc(b))\n";

    double diff = own || !showWall ? -1.0 : diffFromReference("srgb", rgba, target.width, target.height);
    std::cout << label("image");
    if (own) std::cout << "no reference for --albedo\n";
    else if (!showWall) std::cout << "no reference for the tiles view\n";
    else std::cout << imageResult(diff) << std::setprecision(0) << "   (wall, split " << split << ", mode "
                   << params.mode << ")\n";

    if (shaderHasMarker("color.glsl", "TODO(TASK 1a)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1a): srgb_to_linear\n"
                << indent() << "returns c, so tile B is not decoded either, the same as tile C\n"
                << indent() << "and the wall's left side lights the bytes undecoded\n";
    if (shaderHasMarker("color.glsl", "TODO(TASK 1b)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1b): linear_to_srgb\n"
                << indent() << "returns c, so no tile is encoded for the screen\n"
                << indent() << "and neither is the wall's left side\n";
    if (shaderHasMarker("color.glsl", "TODO(TASK 1c)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1c): albedo_check\n"
                << indent() << "returns lin, so mode 1 flags nothing\n";

    std::cout << std::setprecision(3);
    std::cout << label("gpu time") << timing.medianMs << " ms median, " << timing.p95Ms << " ms p95"
              << std::endl;

    if (!args.save.empty()) writePng(args.save, rgba, target.width, target.height);

    if (!args.headless) {
      Window win = createSwapchain(dev, handle);

      bool  paused   = false;
      bool  first    = true;
      bool  dragging = false;
      float clock    = args.time;
      float lastTime = 0.0f;

      auto status = [&](const Params& p) {
        std::size_t m = std::min<std::size_t>(p.mode, 3);
        std::cout << std::setprecision(0) << label("now");
        if (showWall)
          std::cout << "wall, split at x " << p.knobf << ", right of it mode " << p.mode << " "
                    << WALL_MODES[m] << ", lights " << (paused ? "paused" : "moving") << std::endl;
        else
          std::cout << "tiles, mode " << p.mode << " " << TILE_MODES[m] << std::endl;
      };

      FrameHooks hooks{};
      hooks.record    = record;
      hooks.shaders   = { "fullscreen.vert", "srgb_tiles.frag", "srgb_wall.vert", "srgb_wall.frag" };
      hooks.savePath  = std::string(REPORT_DIR) + "/srgb.png";
      hooks.extraKeys =
        "mode, right of the split: 0 gamma-space lighting, 1 albedo_check, 2 no decode, 3 no encode\n"
        "left-drag moves the split; left of it is always the correct pipeline\n"
        "T toggles the wall and the test tiles (tiles: mode 0 shown, 1 albedo_check,\n"
        "  2 linear values raw, 3 |A - B| x 64 on tile B)\n"
        "L pauses and restarts the spotlights";
      hooks.onReload = [&](const std::vector<std::string>&) {
        reloadProgram(dev, tiles);
        reloadProgram(dev, wall);
      };
      hooks.onFrame = [&](Params& p) {
        if (first) status(p);
        if (!first && !paused) clock += p.time - lastTime;
        first    = false;
        lastTime = p.time;
        p.light  = aimsAt(clock);

        double x = 0.0, y = 0.0;
        int    w = 0, h = 0;
        glfwGetCursorPos(win.handle, &x, &y);
        glfwGetWindowSize(win.handle, &w, &h);
        bool down = showWall && glfwGetMouseButton(win.handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (down && w > 0)
          p.knobf = std::clamp(static_cast<float>(x * WIDTH / w), 0.0f, static_cast<float>(WIDTH));
        if (dragging && !down) status(p);
        dragging = down;
      };
      hooks.onKey = [&](int key, Params& p) {
        switch (key) {
          case GLFW_KEY_T: showWall = !showWall; break;
          case GLFW_KEY_L: paused = !paused; break;
          case GLFW_KEY_0:
          case GLFW_KEY_1:
          case GLFW_KEY_2:
          case GLFW_KEY_3: break;
          default:         return;
        }
        status(p);
      };

      runWindow(dev, win, target, uniform, params, args, hooks);
      destroySwapchain(dev, win);
    }

    destroyMesh(dev, wallGpu);
    destroyProgram(dev, wall);
    destroyProgram(dev, tiles);
    vkDestroyPipelineLayout(dev.device, layout, nullptr);
    destroyDescriptors(dev, descs);
    destroyBuffer(dev, uniform);
    destroySamplers(dev, samplers);
    destroyTexture(dev, bricks);
    destroyTexture(dev, raw);
    destroyTexture(dev, decoded);
    destroyPass(dev, pass);
    destroyTarget(dev, target);
    destroyDevice(dev);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
