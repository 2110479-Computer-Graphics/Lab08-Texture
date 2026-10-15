#include "core/Frame.h"
#include "core/Sampler.h"
#include "core/Shader.h"
#include "core/Timer.h"
#include "utils/Color.h"
#include "utils/Hash.h"
#include "utils/image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

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

static bool listed(const glm::uvec2* first, const glm::uvec2* last, glm::uvec2 brick) {
  return std::find(first, last, brick) != last;
}

static std::uint8_t level(int value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

static Image makeTexture() {
  Image image(TEX, TEX);

  for (std::uint32_t y = 0; y < ALBEDO_ROW; ++y)
    for (std::uint32_t x = 0; x < TEX; ++x) {
      std::uint8_t* t = image.at(x, y);
      t[0] = t[1] = t[2] = static_cast<std::uint8_t>(x);
      t[3] = 255;
    }

  for (std::uint32_t y = ALBEDO_ROW; y < TEX; ++y)
    for (std::uint32_t x = 0; x < TEX; ++x) {
      std::uint32_t ly     = y - ALBEDO_ROW;
      std::uint32_t course = ly / BRICK_H;
      std::uint32_t sx     = (x + (course % 2) * (BRICK_W / 2)) % TEX;
      glm::uvec2    brick(course, sx / BRICK_W);
      bool          mortar = ly % BRICK_H < MORTAR || sx % BRICK_W < MORTAR;

      std::uint8_t* t = image.at(x, y);
      t[3] = 255;
      for (std::uint32_t c = 0; c < 3; ++c) {
        int noise = static_cast<int>(pcg(glm::uvec3(x, y, c)) % 7u) - 3;
        int value = 0;
        if (mortar)
          value = (c == 0 ? 168 : c == 1 ? 162 : 152) + noise;
        else if (listed(CHARCOAL.data(), CHARCOAL.data() + CHARCOAL.size(), brick))
          value = 14 + noise;
        else if (listed(WHITE.data(), WHITE.data() + WHITE.size(), brick))
          value = 251 + noise;
        else if (brick == RED)
          value = c == 0 ? 200 : c == 1 ? 30 : 25;
        else {
          float h = hash01(glm::uvec3(brick.x, brick.y, 7u + c));
          int   base[3] = { 140, 78, 62 };
          int   span[3] = { 40, 22, 16 };
          value = base[c] + static_cast<int>(h * static_cast<float>(span[c])) + noise;
        }
        t[c] = level(value);
      }
    }
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

int main(int argc, char** argv) {
  try {
    Args args = parseArgs(argc, argv);

    GLFWwindow* handle =
      args.headless ? nullptr : createHiddenWindow("Lab 08 - Part I", WIDTH, HEIGHT);

    Device dev    = createDevice(handle);
    Target target = createTarget(dev, WIDTH, HEIGHT);
    Pass   pass   = createTargetPass(dev, target);

    bool    own     = !args.albedo.empty();
    Image   albedo  = own ? loadPng(args.albedo) : makeTexture();
    Texture decoded = createTexture(dev, { albedo }, VK_FORMAT_R8G8B8A8_SRGB);
    Texture raw     = createTexture(dev, { albedo }, VK_FORMAT_R8G8B8A8_UNORM);

    Samplers samplers = createSamplers(dev);
    VkSampler nearest = samplers.get(Filter::Nearest, Wrap::Clamp);

    Buffer uniform = createBuffer(dev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Descriptors      descs  = createDescriptors(dev, uniform);
    VkPipelineLayout layout = createPipelineLayout(dev, descs);
    bindTexture(dev, descs.set, 0, decoded.view, nearest);
    bindTexture(dev, descs.set, 1, raw.view, nearest);

    Program srgb = createProgram(dev, pass, layout, "fullscreen.vert", "srgb.frag");

    Params params{};
    params.resolution = glm::vec2(WIDTH, HEIGHT);
    params.time       = args.time;
    params.mode       = args.mode;
    params.knob       = args.knob;
    params.knobf      = args.knobf;
    params.texSize    = glm::vec2(albedo.width, albedo.height);

    auto record = [&](VkCommandBuffer cmd, const Params&) {
      beginPass(cmd, pass);
      bindProgram(cmd, srgb, descs.set);
      drawFullscreen(cmd);
      endPass(cmd);
    };

    Params view = params;
    view.mode   = 0;
    std::vector<std::uint8_t> shown = renderOnce(dev, target, uniform, view, record);
    view.mode   = 1;
    std::vector<std::uint8_t> flags = renderOnce(dev, target, uniform, view, record);
    view.mode   = 2;
    std::vector<std::uint8_t> linear = renderOnce(dev, target, uniform, view, record);

    std::vector<std::uint8_t> rgba = renderOnce(dev, target, uniform, params, record);
    Timing timing = timePass(dev, WARMUP_FRAMES, MEASURED_FRAMES,
                             [&](VkCommandBuffer cmd) { record(cmd, params); });

    std::cout << std::fixed << std::setprecision(4);
    std::cout << label("shaders") << "fullscreen.vert + srgb.frag, which includes color.glsl\n";
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

    double diff = own ? -1.0 : diffFromReference("srgb", rgba, target.width, target.height);
    std::cout << label("image") << (own ? std::string("no reference for --albedo") : imageResult(diff))
              << "\n";

    if (shaderHasMarker("color.glsl", "TODO(TASK 1a)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1a): srgb_to_linear\n"
                << indent() << "returns c, so tile B is not decoded either, the same as tile C\n";
    if (shaderHasMarker("color.glsl", "TODO(TASK 1b)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1b): linear_to_srgb\n"
                << indent() << "returns c, so no tile is encoded for the screen\n";
    if (shaderHasMarker("color.glsl", "TODO(TASK 1c)"))
      std::cout << label("incomplete") << "color.glsl still has TODO(TASK 1c): albedo_check\n"
                << indent() << "returns lin, so mode 1 flags nothing\n";

    std::cout << std::setprecision(3);
    std::cout << label("gpu time") << timing.medianMs << " ms median, " << timing.p95Ms
              << " ms p95" << std::endl;

    if (!args.save.empty()) writePng(args.save, rgba, target.width, target.height);

    if (!args.headless) {
      Window win = createSwapchain(dev, handle);

      FrameHooks hooks{};
      hooks.record    = record;
      hooks.shaders   = { "fullscreen.vert", "srgb.frag" };
      hooks.savePath  = std::string(REPORT_DIR) + "/srgb.png";
      hooks.extraKeys = "mode 0 shown, 1 albedo_check, 2 linear values raw, 3 |A - B| x 64 on tile B";
      hooks.onReload  = [&](const std::vector<std::string>&) { reloadProgram(dev, srgb); };

      runWindow(dev, win, target, uniform, params, args, hooks);
      destroySwapchain(dev, win);
    }

    destroyProgram(dev, srgb);
    vkDestroyPipelineLayout(dev.device, layout, nullptr);
    destroyDescriptors(dev, descs);
    destroyBuffer(dev, uniform);
    destroySamplers(dev, samplers);
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
