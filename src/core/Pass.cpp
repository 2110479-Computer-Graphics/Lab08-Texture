#include "core/Pass.h"

#include <stdexcept>

namespace {
struct Spec {
  VkImageView   view;
  VkFormat      format;
  Load          load;
  VkImageLayout resting;
  VkClearValue  clear;
};

VkAttachmentDescription describe(const Spec& spec) {
  VkAttachmentDescription a{};
  a.format         = spec.format;
  a.samples        = VK_SAMPLE_COUNT_1_BIT;
  a.loadOp         = spec.load == Load::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  a.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  a.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  a.initialLayout  = spec.load == Load::Clear ? VK_IMAGE_LAYOUT_UNDEFINED : spec.resting;
  a.finalLayout    = spec.resting;
  return a;
}

Pass build(const Device& dev, const std::vector<Spec>& specs, bool hasDepth, std::uint32_t width,
           std::uint32_t height)
{
  Pass pass{};
  pass.width      = width;
  pass.height     = height;
  pass.hasDepth   = hasDepth;
  pass.colorCount = static_cast<std::uint32_t>(specs.size()) - (hasDepth ? 1u : 0u);

  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkImageView>             views;
  for (const Spec& spec : specs) {
    attachments.push_back(describe(spec));
    views.push_back(spec.view);
    pass.clears.push_back(spec.clear);
  }

  std::vector<VkAttachmentReference> colorRefs;
  for (std::uint32_t i = 0; i < pass.colorCount; ++i)
    colorRefs.push_back({ i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
  VkAttachmentReference depthRef{ pass.colorCount, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount    = pass.colorCount;
  subpass.pColorAttachments       = colorRefs.data();
  subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

  VkSubpassDependency deps[2]{};
  deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass    = 0;
  deps[0].srcStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  deps[0].dstStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  deps[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  deps[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  deps[1].srcSubpass    = 0;
  deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  deps[1].dstStageMask  = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  deps[1].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

  VkRenderPassCreateInfo renderPassCI{};
  renderPassCI.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount = static_cast<std::uint32_t>(attachments.size());
  renderPassCI.pAttachments    = attachments.data();
  renderPassCI.subpassCount    = 1;
  renderPassCI.pSubpasses      = &subpass;
  renderPassCI.dependencyCount = 2;
  renderPassCI.pDependencies   = deps;
  check(vkCreateRenderPass(dev.device, &renderPassCI, nullptr, &pass.renderPass), "vkCreateRenderPass");

  VkFramebufferCreateInfo fbCI{};
  fbCI.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbCI.renderPass      = pass.renderPass;
  fbCI.attachmentCount = static_cast<std::uint32_t>(views.size());
  fbCI.pAttachments    = views.data();
  fbCI.width           = width;
  fbCI.height          = height;
  fbCI.layers          = 1;
  check(vkCreateFramebuffer(dev.device, &fbCI, nullptr, &pass.framebuffer), "vkCreateFramebuffer");
  return pass;
}

VkClearValue colorClear(glm::vec4 c) {
  VkClearValue v{};
  v.color = { { c.r, c.g, c.b, c.a } };
  return v;
}

VkClearValue depthClear(float d) {
  VkClearValue v{};
  v.depthStencil = { d, 0 };
  return v;
}
}

Pass createTargetPass(const Device& dev, const Target& target, Load load, glm::vec4 clearColor) {
  std::vector<Spec> specs;
  specs.push_back({ target.view, TARGET_FORMAT, load, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    colorClear(clearColor) });
  bool hasDepth = target.depth != VK_NULL_HANDLE;
  if (hasDepth)
    specs.push_back({ target.depthView, DEPTH_FORMAT, load,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthClear(0.0f) });
  return build(dev, specs, hasDepth, target.width, target.height);
}

Pass createPass(const Device& dev, const std::vector<ColorAttachment>& colors,
                const DepthAttachment& depth)
{
  if (colors.empty() || colors.size() > MAX_COLOR_ATTACHMENTS)
    throw std::runtime_error("createPass takes 1 or 2 colour attachments");

  std::uint32_t     width  = colors[0].texture->width;
  std::uint32_t     height = colors[0].texture->height;
  std::vector<Spec> specs;
  for (const ColorAttachment& c : colors) {
    if (c.texture == nullptr || !c.texture->renderTarget)
      throw std::runtime_error("createPass: every colour attachment must be a render texture");
    if (c.texture->width != width || c.texture->height != height)
      throw std::runtime_error("createPass: the colour attachments differ in size");
    specs.push_back({ c.texture->view, c.texture->format, c.load,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, colorClear(c.clear) });
  }

  bool hasDepth = depth.target != nullptr;
  if (hasDepth) {
    if (depth.target->depth == VK_NULL_HANDLE)
      throw std::runtime_error("createPass: that target was made without depth");
    if (depth.target->width != width || depth.target->height != height)
      throw std::runtime_error("createPass: the depth target is not the size of the textures");
    specs.push_back({ depth.target->depthView, DEPTH_FORMAT, depth.load,
                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, depthClear(depth.clear) });
  }
  return build(dev, specs, hasDepth, width, height);
}

void destroyPass(const Device& dev, Pass& pass) {
  vkDestroyFramebuffer(dev.device, pass.framebuffer, nullptr);
  vkDestroyRenderPass(dev.device, pass.renderPass, nullptr);
  pass = Pass{};
}

void beginPass(VkCommandBuffer cmd, const Pass& pass) {
  VkRenderPassBeginInfo passInfo{};
  passInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  passInfo.renderPass        = pass.renderPass;
  passInfo.framebuffer       = pass.framebuffer;
  passInfo.renderArea.extent = { pass.width, pass.height };
  passInfo.clearValueCount   = static_cast<std::uint32_t>(pass.clears.size());
  passInfo.pClearValues      = pass.clears.data();
  vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void endPass(VkCommandBuffer cmd) {
  vkCmdEndRenderPass(cmd);

  VkMemoryBarrier barrier{};
  barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);
}
