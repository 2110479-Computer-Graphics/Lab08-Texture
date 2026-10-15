#include "utils/Args.h"

#include <stdexcept>
#include <vector>

static std::string next(const std::vector<std::string>& args, std::size_t& i, const char* flag) {
  if (i + 1 >= args.size()) throw std::runtime_error(std::string(flag) + " needs a value");
  return args[++i];
}

static std::uint32_t number(const std::string& text) {
  return static_cast<std::uint32_t>(std::stoul(text));
}

Args parseArgs(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  Args parsed{};

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "--headless") {
      parsed.headless    = true;
    } else if (a == "--time") {
      parsed.time        = std::stof(next(args, i, "--time"));
      parsed.timeFixed   = true;
    } else if (a == "--save") {
      parsed.save        = next(args, i, "--save");
    } else if (a == "--mode") {
      parsed.mode        = number(next(args, i, "--mode"));
    } else if (a == "--knob") {
      parsed.knob        = number(next(args, i, "--knob"));
      parsed.knobSet     = true;
    } else if (a == "--knobf") {
      parsed.knobf       = std::stof(next(args, i, "--knobf"));
      parsed.knobfSet    = true;
    } else if (a == "--albedo") {
      parsed.albedo      = next(args, i, "--albedo");
    } else if (a == "--normal") {
      parsed.normal      = next(args, i, "--normal");
    } else if (a == "--dx") {
      parsed.dx          = true;
    } else if (a == "--srgb-normals") {
      parsed.srgbNormals = true;
    } else if (a == "--filter") {
      parsed.filter      = next(args, i, "--filter");
    } else if (a == "--particles") {
      parsed.particles   = number(next(args, i, "--particles"));
    } else if (a == "--method") {
      parsed.method      = next(args, i, "--method");
    } else if (a == "--opt") {
      std::string option = next(args, i, "--opt");
      std::size_t equals = option.find('=');
      if (equals == std::string::npos) throw std::runtime_error("--opt wants name=value");
      parsed.options[option.substr(0, equals)] = option.substr(equals + 1);
    } else {
      throw std::runtime_error("unknown flag: " + a + "\n"
                               "usage: --time T --headless --save out.png --mode m "
                               "--knob n --knobf x\n"
                               "       --albedo f.png (srgb)  --normal f.png --dx "
                               "--srgb-normals (normalmap)\n"
                               "       --filter name (floor)  "
                               "--particles n --method name (oit)");
    }
  }
  return parsed;
}
