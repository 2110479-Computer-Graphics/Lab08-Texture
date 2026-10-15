#ifndef __SHADER_H_INCLUDED__
#define __SHADER_H_INCLUDED__

#include "core/Device.h"

#include <string>

struct Compiled {
  bool        ok = false;
  std::string log;
};

const std::vector<std::string>& shaderSourceDirs();

std::string shaderSourcePath(const std::string& name);

bool shaderHasTodo(const std::string& name);
bool shaderHasMarker(const std::string& name, const std::string& marker);
bool shaderIncludes(const std::string& name, const std::string& needle);

Compiled compileShader(const std::string& name);

VkShaderModule createShaderModule(const Device& dev, const std::string& name);

std::string glslcVersion();

#endif
