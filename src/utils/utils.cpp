#include "utils/utils.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

void check(VkResult result, const char* what) {
  if (result != VK_SUCCESS)
    throw std::runtime_error(std::string(what) + " failed: " + std::to_string(result));
}

std::string label(const std::string& name, std::size_t width) {
  std::string line = "  " + name;
  line.resize(std::max(line.size() + 1, width + 2), ' ');
  return line;
}

std::string indent() {
  return std::string(17, ' ');
}

std::vector<char> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::ate | std::ios::binary);
  if (!f.is_open()) throw std::runtime_error("cannot open: " + path);
  std::size_t size = static_cast<std::size_t>(f.tellg());
  std::vector<char> buf(size);
  f.seekg(0);
  f.read(buf.data(), static_cast<std::streamsize>(size));
  return buf;
}
