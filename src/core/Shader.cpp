#include "core/Shader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

const std::vector<std::string>& shaderSourceDirs() {
  static const std::vector<std::string> dirs = [] {
    std::vector<std::string> out;
    std::stringstream        all(SHADER_SRC_DIRS);
    std::string              dir;
    while (std::getline(all, dir, ';'))
      if (!dir.empty()) out.push_back(dir);
    return out;
  }();
  return dirs;
}

std::string shaderSourcePath(const std::string& name) {
  for (const std::string& dir : shaderSourceDirs()) {
    fs::path path = fs::path(dir) / name;
    if (fs::exists(path)) return path.string();
  }
  return {};
}

static std::string readAll(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open()) return {};
  std::stringstream text;
  text << f.rdbuf();
  return text.str();
}

bool shaderHasTodo(const std::string& name) {
  return shaderHasMarker(name, "TODO(TASK");
}

bool shaderHasMarker(const std::string& name, const std::string& marker) {
  return readAll(shaderSourcePath(name)).find(marker) != std::string::npos;
}

static std::string withoutComments(const std::string& source) {
  std::string out;
  out.reserve(source.size());
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (source.compare(i, 2, "//") == 0) {
      while (i < source.size() && source[i] != '\n') ++i;
      if (i < source.size()) out += '\n';
    } else if (source.compare(i, 2, "/*") == 0) {
      std::size_t end = source.find("*/", i + 2);
      i = end == std::string::npos ? source.size() : end + 1;
    } else {
      out += source[i];
    }
  }
  return out;
}

bool shaderIncludes(const std::string& name, const std::string& needle) {
  return withoutComments(readAll(shaderSourcePath(name))).find(needle) != std::string::npos;
}

static std::string quote(const std::string& text) {
  return "\"" + text + "\"";
}

Compiled compileShader(const std::string& name) {
  Compiled result{};

  std::string source = shaderSourcePath(name);
  if (source.empty()) {
    result.log = "no source file called " + name + " in any shader directory";
    return result;
  }

  fs::path spirv = fs::path(SHADER_DIR) / (name + ".spv");
  fs::path log   = fs::path(SHADER_DIR) / (name + ".log");

  std::string command = quote(GLSLC);
  for (const std::string& dir : shaderSourceDirs())
    command += " " + quote("-I" + dir);
  command += " " + quote(source) + " -o " + quote(spirv.string());
  command += " > " + quote(log.string()) + " 2>&1";
#ifdef _WIN32
  command = quote(command);
#endif

  result.ok  = std::system(command.c_str()) == 0;
  result.log = readAll(log.string());
  return result;
}

std::string glslcVersion() {
  fs::path    log     = fs::path(SHADER_DIR) / "glslc.log";
  std::string command = quote(GLSLC) + " --version > " + quote(log.string()) + " 2>&1";
#ifdef _WIN32
  command = quote(command);
#endif
  if (std::system(command.c_str()) != 0) return {};

  std::string  first;
  std::ifstream f(log.string());
  std::getline(f, first);
  return first;
}

VkShaderModule createShaderModule(const Device& dev, const std::string& name) {
  std::vector<char> code = readFile((fs::path(SHADER_DIR) / (name + ".spv")).string());

  VkShaderModuleCreateInfo moduleCI{};
  moduleCI.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  moduleCI.codeSize = code.size();
  moduleCI.pCode    = reinterpret_cast<const std::uint32_t*>(code.data());

  VkShaderModule shader = VK_NULL_HANDLE;
  check(vkCreateShaderModule(dev.device, &moduleCI, nullptr, &shader), "vkCreateShaderModule");
  return shader;
}
