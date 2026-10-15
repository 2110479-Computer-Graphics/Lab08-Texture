#ifndef __IMAGE_H_INCLUDED__
#define __IMAGE_H_INCLUDED__

#include <cstdint>
#include <string>
#include <vector>

struct Image {
  std::uint32_t             width  = 0;
  std::uint32_t             height = 0;
  std::vector<std::uint8_t> rgba;

  Image() = default;
  Image(std::uint32_t w, std::uint32_t h) : width(w), height(h), rgba(std::size_t(w) * h * 4, 0) {}

  std::uint8_t*       at(std::uint32_t x, std::uint32_t y)       { return &rgba[(std::size_t(y) * width + x) * 4]; }
  const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const { return &rgba[(std::size_t(y) * width + x) * 4]; }
};

Image loadPng(const std::string& path);

std::vector<Image> boxMips(const Image& level0);

void writePng(const std::string& path,
              const std::vector<std::uint8_t>& rgba,
              std::uint32_t width,
              std::uint32_t height);

void writePng(const std::string& path, const Image& image);

double coverage(const std::vector<std::uint8_t>& rgba);

double diffFromReference(const std::string& name,
                         const std::vector<std::uint8_t>& rgba,
                         std::uint32_t width,
                         std::uint32_t height);

bool matches(double diffPercent);

std::string imageResult(double diffPercent);

std::vector<std::uint8_t> pixelAt(const std::vector<std::uint8_t>& rgba,
                                  std::uint32_t width,
                                  std::uint32_t x,
                                  std::uint32_t y);

#endif
