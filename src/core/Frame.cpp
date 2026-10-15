#include "core/Frame.h"

#include "core/Shader.h"
#include "core/Timer.h"
#include "utils/image.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

using Clock  = std::chrono::steady_clock;
using Stamps = std::map<std::string, fs::file_time_type>;

static std::vector<int> pendingKeys;
static double           pendingScroll = 0.0;

static void keyCallback(GLFWwindow*, int key, int, int action, int) {
  if (action == GLFW_PRESS) pendingKeys.push_back(key);
}

static void scrollCallback(GLFWwindow*, double, double dy) {
  pendingScroll += dy;
}

std::vector<std::uint8_t> renderOnce(const Device& dev,
                                     const Target& target,
                                     const Buffer& uniform,
                                     const Params& params,
                                     const std::function<void(VkCommandBuffer, const Params&)>& record)
{
  uploadBuffer(uniform, &params, sizeof(Params));
  timePass(dev, 0, 1, [&](VkCommandBuffer cmd) { record(cmd, params); }, 1);
  return readTarget(dev, target);
}

Program createProgram(const Device& dev,
                      const Pass& pass,
                      VkPipelineLayout layout,
                      const std::string& vertName,
                      const std::string& fragName,
                      const VertexLayout& vertexLayout,
                      const PipelineState& state)
{
  Program program{};
  program.vertName     = vertName;
  program.fragName     = fragName;
  program.vertexLayout = vertexLayout;
  program.state        = state;
  program.pass         = pass;
  program.layout       = layout;
  program.vert         = createShaderModule(dev, vertName);
  program.frag         = createShaderModule(dev, fragName);
  program.pipeline     = createPipeline(dev, pass, layout, program.vert, program.frag,
                                        vertexLayout, state);
  return program;
}

void reloadProgram(const Device& dev, Program& program) {
  Program old = program;
  destroyProgram(dev, program);
  program = createProgram(dev, old.pass, old.layout, old.vertName, old.fragName, old.vertexLayout,
                          old.state);
}

void destroyProgram(const Device& dev, Program& program) {
  vkDestroyPipeline(dev.device, program.pipeline, nullptr);
  vkDestroyShaderModule(dev.device, program.frag, nullptr);
  vkDestroyShaderModule(dev.device, program.vert, nullptr);
  program = Program{};
}

void bindProgram(VkCommandBuffer cmd, const Program& program, VkDescriptorSet set) {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, 1, &set, 0,
                          nullptr);
}

void drawFullscreen(VkCommandBuffer cmd) {
  vkCmdDraw(cmd, 3, 1, 0, 0);
}

static Stamps shaderStamps() {
  Stamps stamps;
  for (const std::string& dir : shaderSourceDirs()) {
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
      if (entry.is_regular_file(ec))
        stamps[entry.path().string()] = entry.last_write_time(ec);
  }
  return stamps;
}

static void printLog(const std::string& log) {
  std::istringstream lines(log);
  bool               first = true;
  for (std::string text; std::getline(lines, text);) {
    if (text.empty()) continue;
    std::cout << (first ? label("compile error") : indent()) << text << "\n";
    first = false;
  }
  if (first) std::cout << label("compile error") << "glslc said nothing, look at the file\n";
}

static void reload(const FrameHooks& hooks) {
  bool ok = true;
  for (const std::string& name : hooks.shaders) {
    Compiled compiled = compileShader(name);
    if (!compiled.ok) {
      ok = false;
      printLog(compiled.log);
    }
  }
  if (!ok) {
    std::cout << std::flush;
    return;
  }

  if (hooks.onReload) hooks.onReload(hooks.shaders);
  for (const std::string& name : hooks.shaders)
    std::cout << label("reloaded") << name << "\n";
  std::cout << std::flush;
}

static void printKeys(const FrameHooks& hooks) {
  std::cout << label("keys") << "0..3 debug modes, S saves a png, Esc quits\n";
  if (hooks.camera != nullptr)
    std::cout << indent() << "left-drag orbits the camera, the scroll wheel moves it in and out\n";
  std::istringstream extra(hooks.extraKeys);
  for (std::string line; std::getline(extra, line);)
    if (!line.empty()) std::cout << indent() << line << "\n";
  for (const std::string& dir : shaderSourceDirs())
    std::cout << label("watching") << dir << "\n";
  std::cout << std::flush;
}

static void handleKey(int key, Params& params, const Device& dev, const Target& target,
                      const FrameHooks& hooks)
{
  switch (key) {
    case GLFW_KEY_0: params.mode = 0; break;
    case GLFW_KEY_1: params.mode = 1; break;
    case GLFW_KEY_2: params.mode = 2; break;
    case GLFW_KEY_3: params.mode = 3; break;
    case GLFW_KEY_S:
      if (!hooks.savePath.empty()) {
        fs::create_directories(fs::path(hooks.savePath).parent_path());
        writePng(hooks.savePath, readTarget(dev, target), target.width, target.height);
        std::cout << label("saved") << hooks.savePath << std::endl;
      }
      break;
    default: break;
  }
  if (hooks.onKey) hooks.onKey(key, params);
}

void runWindow(const Device& dev,
               const Window& win,
               const Target& target,
               const Buffer& uniform,
               Params& params,
               const Args& args,
               const FrameHooks& hooks)
{
  printKeys(hooks);
  pendingKeys.clear();
  pendingScroll = 0.0;
  glfwSetKeyCallback(win.handle, keyCallback);
  glfwSetScrollCallback(win.handle, scrollCallback);
  showWindow(win);

  Stamps            stamps  = shaderStamps();
  Clock::time_point started = Clock::now();
  Clock::time_point polled  = started;
  bool              dragging = false;
  double            lastX    = 0.0;
  double            lastY    = 0.0;

  while (windowOpen(win)) {
    Clock::time_point now = Clock::now();

    std::chrono::duration<float> elapsed = now - started;
    params.time = args.timeFixed ? args.time : elapsed.count();

    if (hooks.camera != nullptr) {
      double x = 0.0;
      double y = 0.0;
      glfwGetCursorPos(win.handle, &x, &y);
      bool down = glfwGetMouseButton(win.handle, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
      if (down && dragging)
        hooks.camera->orbit(static_cast<float>(x - lastX), static_cast<float>(y - lastY));
      dragging = down;
      lastX    = x;
      lastY    = y;
      if (pendingScroll != 0.0) hooks.camera->zoom(static_cast<float>(pendingScroll));
      pendingScroll = 0.0;

      params.view = hooks.camera->view();
      params.eye  = glm::vec4(hooks.camera->eye(), 1.0f);
    }
    if (hooks.onFrame) hooks.onFrame(params);

    uploadBuffer(uniform, &params, sizeof(Params));
    timePass(dev, 0, 1, [&](VkCommandBuffer cmd) { hooks.record(cmd, params); }, 1);

    std::vector<int> keys;
    keys.swap(pendingKeys);
    for (int key : keys) {
      if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win.handle, GLFW_TRUE);
      else handleKey(key, params, dev, target, hooks);
    }

    if (now - polled > std::chrono::milliseconds(250)) {
      polled        = now;
      Stamps latest = shaderStamps();
      if (latest != stamps) {
        stamps = latest;
        vkDeviceWaitIdle(dev.device);
        reload(hooks);
      }
    }

    presentTarget(dev, win, target);
  }

  glfwSetKeyCallback(win.handle, nullptr);
  glfwSetScrollCallback(win.handle, nullptr);
}
