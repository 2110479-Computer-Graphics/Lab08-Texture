#ifndef __PASS_H_INCLUDED__
#define __PASS_H_INCLUDED__

#include "core/Target.h"
#include "core/Texture.h"

#include <glm/glm.hpp>

enum class Load { Clear, Keep };

inline constexpr std::uint32_t MAX_COLOR_ATTACHMENTS = 2;

struct ColorAttachment {
  const Texture* texture = nullptr;
  Load           load    = Load::Clear;
  glm::vec4      clear   = glm::vec4(0.0f);
};

struct DepthAttachment {
  const Target* target = nullptr;
  Load          load   = Load::Keep;
  float         clear  = 0.0f;
};

struct Pass {
  VkRenderPass              renderPass  = VK_NULL_HANDLE;
  VkFramebuffer             framebuffer = VK_NULL_HANDLE;
  std::uint32_t             width       = 0;
  std::uint32_t             height      = 0;
  std::uint32_t             colorCount  = 0;
  bool                      hasDepth    = false;
  std::vector<VkClearValue> clears;
};

Pass createTargetPass(const Device& dev, const Target& target, Load load = Load::Clear,
                      glm::vec4 clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
Pass createPass(const Device& dev, const std::vector<ColorAttachment>& colors,
                const DepthAttachment& depth = DepthAttachment{});
void destroyPass(const Device& dev, Pass& pass);

void beginPass(VkCommandBuffer cmd, const Pass& pass);
void endPass(VkCommandBuffer cmd);

#endif
