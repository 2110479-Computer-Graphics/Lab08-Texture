#ifndef __FRAME_H_INCLUDED__
#define __FRAME_H_INCLUDED__

#include "core/Pipeline.h"
#include "core/Window.h"
#include "utils/Args.h"
#include "utils/Camera.h"
#include "utils/Params.h"

#include <functional>
#include <string>

struct Program {
  std::string      vertName;
  std::string      fragName;
  VertexLayout     vertexLayout;
  PipelineState    state;
  Pass             pass;
  VkPipelineLayout layout   = VK_NULL_HANDLE;
  VkShaderModule   vert     = VK_NULL_HANDLE;
  VkShaderModule   frag     = VK_NULL_HANDLE;
  VkPipeline       pipeline = VK_NULL_HANDLE;
};

Program createProgram(const Device& dev,
                      const Pass& pass,
                      VkPipelineLayout layout,
                      const std::string& vertName,
                      const std::string& fragName,
                      const VertexLayout& vertexLayout = VertexLayout{},
                      const PipelineState& state = PipelineState{});

void reloadProgram(const Device& dev, Program& program);

void destroyProgram(const Device& dev, Program& program);

void bindProgram(VkCommandBuffer cmd, const Program& program, VkDescriptorSet set);

void drawFullscreen(VkCommandBuffer cmd);

struct FrameHooks {
  std::function<void(VkCommandBuffer, const Params&)>  record;
  std::vector<std::string>                             shaders;
  std::function<void(const std::vector<std::string>&)> onReload;
  std::function<void(int, Params&)>                    onKey;
  std::function<void(Params&)>                         onFrame;
  std::string                                          extraKeys;
  std::string                                          savePath;
  OrbitCamera*                                         camera = nullptr;
};

std::vector<std::uint8_t> renderOnce(const Device& dev,
                                     const Target& target,
                                     const Buffer& uniform,
                                     const Params& params,
                                     const std::function<void(VkCommandBuffer, const Params&)>& record);

void runWindow(const Device& dev,
               const Window& win,
               const Target& target,
               const Buffer& uniform,
               Params& params,
               const Args& args,
               const FrameHooks& hooks);

#endif
