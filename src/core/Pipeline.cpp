#include "core/Pipeline.h"

#include <stdexcept>

Descriptors createDescriptors(const Device& dev, const Buffer& uniform, std::uint32_t setCount) {
  if (setCount == 0) throw std::runtime_error("createDescriptors needs at least one set");
  Descriptors descs{};

  VkDescriptorSetLayoutBinding bindings[1 + TEXTURE_SLOTS]{};
  bindings[0].binding         = 0;
  bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  for (std::uint32_t i = 1; i <= TEXTURE_SLOTS; ++i) {
    bindings[i].binding         = i;
    bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo layoutCI{};
  layoutCI.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutCI.bindingCount = 1 + TEXTURE_SLOTS;
  layoutCI.pBindings    = bindings;
  check(vkCreateDescriptorSetLayout(dev.device, &layoutCI, nullptr, &descs.layout),
        "vkCreateDescriptorSetLayout");

  VkDescriptorPoolSize poolSizes[2]{};
  poolSizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = setCount;
  poolSizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = setCount * TEXTURE_SLOTS;

  VkDescriptorPoolCreateInfo poolCI{};
  poolCI.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolCI.maxSets       = setCount;
  poolCI.poolSizeCount = 2;
  poolCI.pPoolSizes    = poolSizes;
  check(vkCreateDescriptorPool(dev.device, &poolCI, nullptr, &descs.pool), "vkCreateDescriptorPool");

  std::vector<VkDescriptorSetLayout> layouts(setCount, descs.layout);
  descs.sets.resize(setCount);

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool     = descs.pool;
  allocInfo.descriptorSetCount = setCount;
  allocInfo.pSetLayouts        = layouts.data();
  check(vkAllocateDescriptorSets(dev.device, &allocInfo, descs.sets.data()), "vkAllocateDescriptorSets");
  descs.set = descs.sets[0];

  Image white(1, 1);
  white.rgba = { 255, 255, 255, 255 };
  descs.dummy = createTexture(dev, { white }, VK_FORMAT_R8G8B8A8_UNORM);

  VkSamplerCreateInfo samplerCI{};
  samplerCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter    = VK_FILTER_NEAREST;
  samplerCI.minFilter    = VK_FILTER_NEAREST;
  samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  check(vkCreateSampler(dev.device, &samplerCI, nullptr, &descs.dummySampler), "vkCreateSampler");

  for (VkDescriptorSet set : descs.sets) {
    bindUniform(dev, set, uniform);
    for (std::uint32_t slot = 0; slot < TEXTURE_SLOTS; ++slot)
      bindTexture(dev, set, slot, descs.dummy.view, descs.dummySampler);
  }
  return descs;
}

void destroyDescriptors(const Device& dev, Descriptors& descs) {
  vkDestroySampler(dev.device, descs.dummySampler, nullptr);
  destroyTexture(dev, descs.dummy);
  vkDestroyDescriptorPool(dev.device, descs.pool, nullptr);
  vkDestroyDescriptorSetLayout(dev.device, descs.layout, nullptr);
  descs = Descriptors{};
}

void bindTexture(const Device& dev, VkDescriptorSet set, std::uint32_t slot, VkImageView view,
                 VkSampler sampler)
{
  if (slot >= TEXTURE_SLOTS) throw std::runtime_error("bindTexture: slot is 0, 1 or 2");

  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler     = sampler;
  imageInfo.imageView   = view;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write{};
  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet          = set;
  write.dstBinding      = 1 + slot;
  write.descriptorCount = 1;
  write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo      = &imageInfo;
  vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
}

void bindUniform(const Device& dev, VkDescriptorSet set, const Buffer& uniform) {
  VkDescriptorBufferInfo bufInfo{};
  bufInfo.buffer = uniform.handle;
  bufInfo.offset = 0;
  bufInfo.range  = uniform.size;

  VkWriteDescriptorSet write{};
  write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet          = set;
  write.dstBinding      = 0;
  write.descriptorCount = 1;
  write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write.pBufferInfo     = &bufInfo;
  vkUpdateDescriptorSets(dev.device, 1, &write, 0, nullptr);
}

VkPipelineLayout createPipelineLayout(const Device& dev, const Descriptors& descs,
                                     std::uint32_t pushSize)
{
  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.size       = pushSize;

  VkPipelineLayoutCreateInfo layoutCI{};
  layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutCI.setLayoutCount         = 1;
  layoutCI.pSetLayouts            = &descs.layout;
  layoutCI.pushConstantRangeCount = pushSize > 0 ? 1u : 0u;
  layoutCI.pPushConstantRanges    = &push;

  VkPipelineLayout layout = VK_NULL_HANDLE;
  check(vkCreatePipelineLayout(dev.device, &layoutCI, nullptr, &layout), "vkCreatePipelineLayout");
  return layout;
}

static VkPipelineColorBlendAttachmentState blendState(BlendMode mode) {
  VkPipelineColorBlendAttachmentState s{};
  s.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  s.colorBlendOp   = VK_BLEND_OP_ADD;
  s.alphaBlendOp   = VK_BLEND_OP_ADD;

  switch (mode) {
    case BlendMode::None:
      s.blendEnable = VK_FALSE;
      break;
    case BlendMode::Over:
      s.blendEnable         = VK_TRUE;
      s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      break;
    case BlendMode::Additive:
      s.blendEnable         = VK_TRUE;
      s.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      break;
    case BlendMode::Revealage:
      s.blendEnable         = VK_TRUE;
      s.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
      s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
      s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      break;
  }
  return s;
}

VkPipeline createPipeline(const Device& dev,
                          const Pass& pass,
                          VkPipelineLayout layout,
                          VkShaderModule vertShader,
                          VkShaderModule fragShader,
                          const VertexLayout& vertexLayout,
                          const PipelineState& state)
{
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertShader;
  stages[0].pName  = "main";
  stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragShader;
  stages[1].pName  = "main";

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount   = static_cast<std::uint32_t>(vertexLayout.bindings.size());
  vertexInput.pVertexBindingDescriptions      = vertexLayout.bindings.data();
  vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexLayout.attrs.size());
  vertexInput.pVertexAttributeDescriptions    = vertexLayout.attrs.data();

  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = state.topology;

  VkViewport viewport{};
  viewport.width    = static_cast<float>(pass.width);
  viewport.height   = static_cast<float>(pass.height);
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.extent = { pass.width, pass.height };

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports    = &viewport;
  viewportState.scissorCount  = 1;
  viewportState.pScissors     = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode    = state.cullMode;
  raster.frontFace   = state.frontFace;
  raster.lineWidth   = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  if (pass.colorCount > 1 && state.blend[0] != state.blend[1] && !dev.hasIndependentBlend)
    throw std::runtime_error("this GPU cannot blend two attachments differently "
                             "(no independentBlend)");

  std::array<VkPipelineColorBlendAttachmentState, MAX_COLOR_ATTACHMENTS> blendAttachments{};
  for (std::uint32_t i = 0; i < pass.colorCount; ++i)
    blendAttachments[i] = blendState(state.blend[i]);

  VkPipelineColorBlendStateCreateInfo blend{};
  blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = pass.colorCount;
  blend.pAttachments    = blendAttachments.data();

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthTestEnable  = state.depthCompare != VK_COMPARE_OP_NEVER;
  depthStencil.depthWriteEnable = state.depthWrite;
  depthStencil.depthCompareOp   = state.depthCompare;
  depthStencil.maxDepthBounds   = 1.0f;

  VkGraphicsPipelineCreateInfo pipelineCI{};
  pipelineCI.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCI.stageCount          = 2;
  pipelineCI.pStages             = stages;
  pipelineCI.pVertexInputState   = &vertexInput;
  pipelineCI.pInputAssemblyState = &assembly;
  pipelineCI.pViewportState      = &viewportState;
  pipelineCI.pRasterizationState = &raster;
  pipelineCI.pMultisampleState   = &multisample;
  pipelineCI.pDepthStencilState  = &depthStencil;
  pipelineCI.pColorBlendState    = &blend;
  pipelineCI.layout              = layout;
  pipelineCI.renderPass          = pass.renderPass;

  VkPipeline pipeline = VK_NULL_HANDLE;
  check(vkCreateGraphicsPipelines(dev.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline),
        "vkCreateGraphicsPipelines");
  return pipeline;
}
