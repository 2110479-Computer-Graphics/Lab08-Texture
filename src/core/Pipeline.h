#ifndef __PIPELINE_H_INCLUDED__
#define __PIPELINE_H_INCLUDED__

#include "core/Buffer.h"
#include "core/Pass.h"
#include "core/Texture.h"

#include <array>
#include <string>

inline constexpr std::uint32_t TEXTURE_SLOTS = 3;

struct Descriptors {
  VkDescriptorSetLayout        layout = VK_NULL_HANDLE;
  VkDescriptorPool             pool   = VK_NULL_HANDLE;
  std::vector<VkDescriptorSet> sets;
  VkDescriptorSet              set    = VK_NULL_HANDLE;
  Texture                      dummy;
  VkSampler                    dummySampler = VK_NULL_HANDLE;
};

Descriptors createDescriptors(const Device& dev, const Buffer& uniform, std::uint32_t setCount = 1);
void        destroyDescriptors(const Device& dev, Descriptors& descs);

void bindTexture(const Device& dev, VkDescriptorSet set, std::uint32_t slot, VkImageView view,
                 VkSampler sampler);

void bindUniform(const Device& dev, VkDescriptorSet set, const Buffer& uniform);

VkPipelineLayout createPipelineLayout(const Device& dev, const Descriptors& descs,
                                      std::uint32_t pushSize = 0);

struct VertexLayout {
  std::vector<VkVertexInputBindingDescription>   bindings;
  std::vector<VkVertexInputAttributeDescription> attrs;

  bool empty() const { return bindings.empty(); }

  VertexLayout& binding(std::uint32_t index, std::uint32_t stride,
                        VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX)
  {
    bindings.push_back({ index, stride, rate });
    return *this;
  }

  VertexLayout& attr(std::uint32_t location, std::uint32_t bindingIndex, VkFormat format,
                     std::uint32_t offset)
  {
    attrs.push_back({ location, bindingIndex, format, offset });
    return *this;
  }
};

enum class BlendMode { None, Over, Additive, Revealage };

struct PipelineState {
  VkFrontFace         frontFace    = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  VkCullModeFlags     cullMode     = VK_CULL_MODE_NONE;
  VkCompareOp         depthCompare = VK_COMPARE_OP_NEVER;
  bool                depthWrite   = false;
  VkPrimitiveTopology topology     = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  std::array<BlendMode, MAX_COLOR_ATTACHMENTS> blend = { BlendMode::None, BlendMode::None };
};

VkPipeline createPipeline(const Device& dev,
                          const Pass& pass,
                          VkPipelineLayout layout,
                          VkShaderModule vertShader,
                          VkShaderModule fragShader,
                          const VertexLayout& vertexLayout = VertexLayout{},
                          const PipelineState& state = PipelineState{});

#endif
