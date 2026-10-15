#include "utils/image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

Image loadPng(const std::string& path) {
  int width    = 0;
  int height   = 0;
  int channels = 0;
  std::uint8_t* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
  if (pixels == nullptr)
    throw std::runtime_error("cannot read image " + path + ": " + stbi_failure_reason());

  Image image(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
  std::memcpy(image.rgba.data(), pixels, image.rgba.size());
  stbi_image_free(pixels);
  return image;
}

std::vector<Image> boxMips(const Image& level0) {
  std::vector<Image> chain;
  chain.push_back(level0);

  while (chain.back().width > 1 || chain.back().height > 1) {
    const Image& above = chain.back();
    Image        level(std::max(above.width / 2, 1u), std::max(above.height / 2, 1u));

    for (std::uint32_t y = 0; y < level.height; ++y)
      for (std::uint32_t x = 0; x < level.width; ++x) {
        std::uint32_t x0 = std::min(2 * x, above.width - 1);
        std::uint32_t x1 = std::min(2 * x + 1, above.width - 1);
        std::uint32_t y0 = std::min(2 * y, above.height - 1);
        std::uint32_t y1 = std::min(2 * y + 1, above.height - 1);
        for (std::uint32_t c = 0; c < 4; ++c) {
          std::uint32_t sum = above.at(x0, y0)[c] + above.at(x1, y0)[c] +
                              above.at(x0, y1)[c] + above.at(x1, y1)[c];
          level.at(x, y)[c] = static_cast<std::uint8_t>((sum + 2) / 4);
        }
      }
    chain.push_back(std::move(level));
  }
  return chain;
}

void writePng(const std::string& path,
              const std::vector<std::uint8_t>& rgba,
              std::uint32_t width,
              std::uint32_t height)
{
  stbi_write_png(path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                 static_cast<int>(width) * 4);
}

void writePng(const std::string& path, const Image& image) {
  writePng(path, image.rgba, image.width, image.height);
}

double coverage(const std::vector<std::uint8_t>& rgba) {
  std::size_t drawn = 0;
  for (std::size_t i = 0; i < rgba.size(); i += 4)
    if (rgba[i] != 0 || rgba[i + 1] != 0 || rgba[i + 2] != 0) ++drawn;
  return static_cast<double>(drawn) / static_cast<double>(rgba.size() / 4);
}

double diffFromReference(const std::string& name,
                         const std::vector<std::uint8_t>& rgba,
                         std::uint32_t width,
                         std::uint32_t height)
{
  std::string path = std::string(REFERENCE_DIR) + "/" + name + ".png";

  int refWidth  = 0;
  int refHeight = 0;
  int channels  = 0;
  std::uint8_t* ref = stbi_load(path.c_str(), &refWidth, &refHeight, &channels, 4);
  if (ref == nullptr) return -1.0;

  if (static_cast<std::uint32_t>(refWidth) != width ||
      static_cast<std::uint32_t>(refHeight) != height) {
    stbi_image_free(ref);
    return 100.0;
  }

  std::size_t different = 0;
  for (std::size_t i = 0; i < rgba.size(); i += 4)
    for (std::size_t c = 0; c < 3; ++c)
      if (std::abs(static_cast<int>(rgba[i + c]) - static_cast<int>(ref[i + c])) > 3) {
        ++different;
        break;
      }

  stbi_image_free(ref);
  return 100.0 * static_cast<double>(different) / static_cast<double>(rgba.size() / 4);
}

bool matches(double diffPercent) {
  return diffPercent >= 0.0 && diffPercent < 1.0;
}

std::string imageResult(double diffPercent) {
  if (diffPercent < 0.0) return "no reference";

  std::ostringstream line;
  line << std::fixed << std::setprecision(2);
  line << (matches(diffPercent) ? "MATCH (" : "DIFFERS (") << diffPercent << "% differ)";
  return line.str();
}

std::vector<std::uint8_t> pixelAt(const std::vector<std::uint8_t>& rgba,
                                  std::uint32_t width,
                                  std::uint32_t x,
                                  std::uint32_t y)
{
  std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
  return { rgba[i], rgba[i + 1], rgba[i + 2] };
}
