#ifndef __ARGS_H_INCLUDED__
#define __ARGS_H_INCLUDED__

#include <cstdint>
#include <map>
#include <string>

struct Args {
  float         time      = 1.0f;
  bool          timeFixed = false;
  bool          headless  = false;
  std::string   save;
  std::uint32_t mode      = 0;
  std::uint32_t knob      = 0;
  bool          knobSet   = false;
  float         knobf     = 0.0f;
  bool          knobfSet  = false;

  std::string   albedo;
  std::string   normal;
  bool          dx          = false;
  bool          srgbNormals = false;
  std::string   filter;
  std::uint32_t particles = 0;
  std::string   method;

  std::map<std::string, std::string> options;
};

Args parseArgs(int argc, char** argv);

#endif
