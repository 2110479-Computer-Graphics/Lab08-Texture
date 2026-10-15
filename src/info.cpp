#include "core/Frame.h"
#include "core/Sampler.h"
#include "core/Shader.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

struct TaskFile {
  const char* name;
  const char* task;
};

static const std::array<TaskFile, 6> TASK_FILES = {{
  { "color.glsl",           "TASK 1" },
  { "normalmap.vert",       "TASK 2" },
  { "normalmap.frag",       "TASK 2" },
  { "miplevel.frag",        "TASK 3" },
  { "wboit_accum.frag",     "TASK 4" },
  { "wboit_composite.frag", "TASK 4" },
}};

static bool formatHas(const Device& dev, VkFormat format, VkFormatFeatureFlags wanted) {
  VkFormatProperties props{};
  vkGetPhysicalDeviceFormatProperties(dev.gpu, format, &props);
  return (props.optimalTilingFeatures & wanted) == wanted;
}

static const char* yesNo(bool value) { return value ? "yes" : "NO"; }

static std::string queryLodTest(const Device& dev) {
  constexpr std::uint32_t TEX  = 256;
  constexpr std::uint32_t SIZE = 64;

  Image level0(TEX, TEX);
  for (std::uint8_t& b : level0.rgba) b = 128;

  Texture  texture  = createTexture(dev, boxMips(level0), VK_FORMAT_R8G8B8A8_UNORM);
  Texture  probe    = createRenderTexture(dev, SIZE, SIZE, VK_FORMAT_R32G32B32A32_SFLOAT);
  Samplers samplers = createSamplers(dev);
  Pass     pass     = createPass(dev, { { &probe } });

  Buffer uniform = createBuffer(dev, sizeof(Params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  Params params{};
  params.resolution = glm::vec2(SIZE, SIZE);
  params.texSize    = glm::vec2(TEX, TEX);
  uploadBuffer(uniform, &params, sizeof(Params));

  Descriptors      descs  = createDescriptors(dev, uniform);
  VkPipelineLayout layout = createPipelineLayout(dev, descs);
  bindTexture(dev, descs.set, 0, texture.view, samplers.get(Filter::Trilinear, Wrap::Clamp));

  std::ostringstream result;
  result << std::fixed << std::setprecision(2);
  try {
    Program program = createProgram(dev, pass, layout, "fullscreen.vert", "querylod.frag");
    submitNow(dev, [&](VkCommandBuffer cmd) {
      beginPass(cmd, pass);
      bindProgram(cmd, program, descs.set);
      drawFullscreen(cmd);
      endPass(cmd);
    });
    std::vector<float> lod = readTextureFloats(dev, probe);
    std::size_t        i   = (std::size_t(SIZE / 2) * SIZE + SIZE / 2) * 4;
    float              got = lod[i];
    bool               ok  = std::isfinite(got) && std::fabs(got - 2.0f) < 0.1f;
    result << (ok ? "works: " : "RUNS BUT IS WRONG: ") << "lod " << got << " (raw " << lod[i + 1]
           << ") for 4 texels a pixel, expected 2.00";
    destroyProgram(dev, program);
  } catch (const std::exception& e) {
    result << "UNSUPPORTED here (" << e.what() << "): Part III prints n/a";
  }

  vkDestroyPipelineLayout(dev.device, layout, nullptr);
  destroyDescriptors(dev, descs);
  destroyBuffer(dev, uniform);
  destroyPass(dev, pass);
  destroySamplers(dev, samplers);
  destroyTexture(dev, probe);
  destroyTexture(dev, texture);
  return result.str();
}

int main() {
  try {
    Device dev = createDevice(nullptr);

    std::cout << label("device") << dev.props.deviceName << "\n";
    if (!dev.driverName.empty())
      std::cout << label("driver") << dev.driverName << " " << dev.driverInfo << "\n";
    std::cout << label("api") << VK_API_VERSION_MAJOR(dev.props.apiVersion) << "."
              << VK_API_VERSION_MINOR(dev.props.apiVersion) << "."
              << VK_API_VERSION_PATCH(dev.props.apiVersion) << "\n";

    if (dev.hasTimestamps)
      std::cout << label("timestamps") << dev.props.limits.timestampPeriod << " ns per tick\n";
    else
      std::cout << label("timestamps") << "UNSUPPORTED: this driver cannot measure the sweeps,\n"
                << indent() << "see 'If it does not work' in the handout\n";

    std::cout << label("anisotropy") << "samplerAnisotropy "
              << (dev.hasAnisotropy ? "enabled" : "UNSUPPORTED") << ", maxSamplerAnisotropy "
              << dev.props.limits.maxSamplerAnisotropy << "\n";
    std::cout << label("blend") << "independentBlend " << yesNo(dev.hasIndependentBlend)
              << " (two attachments, two different blends)\n";

    std::cout << label("srgb") << "R8G8B8A8_SRGB sampled with linear filtering: "
              << yesNo(formatHas(dev, VK_FORMAT_R8G8B8A8_SRGB,
                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                   VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
              << "\n";

    const VkFormatFeatureFlags blendable =
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    std::cout << label("float targets") << "blending into R16G16B16A16_SFLOAT "
              << yesNo(formatHas(dev, VK_FORMAT_R16G16B16A16_SFLOAT, blendable))
              << ", R16_SFLOAT " << yesNo(formatHas(dev, VK_FORMAT_R16_SFLOAT, blendable))
              << ", R8_UNORM " << yesNo(formatHas(dev, VK_FORMAT_R8_UNORM, blendable)) << "\n";

    std::cout << label("queryLod") << "textureQueryLod " << queryLodTest(dev) << "\n";

    std::cout << label("glslc") << GLSLC << "\n";
    std::cout << label("version") << glslcVersion() << "\n";

    for (const std::string& dir : shaderSourceDirs())
      std::cout << label("shaders") << dir << "\n";

    for (const TaskFile& file : TASK_FILES) {
      if (shaderSourcePath(file.name).empty())
        std::cout << label(file.name, 21) << file.task << " MISSING\n";
      else
        std::cout << label(file.name, 21) << file.task << " "
                  << (shaderHasTodo(file.name) ? "incomplete" : "done") << "\n";
    }

    destroyDevice(dev);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
