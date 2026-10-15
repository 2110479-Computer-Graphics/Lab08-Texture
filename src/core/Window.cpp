#include "core/Window.h"

#include <algorithm>
#include <stdexcept>

GLFWwindow* createHiddenWindow(const char* title, std::uint32_t width, std::uint32_t height) {
  if (!glfwInit()) throw std::runtime_error("glfwInit failed");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

  GLFWwindow* handle = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title,
                                        nullptr, nullptr);
  if (!handle) throw std::runtime_error("glfwCreateWindow failed");
  return handle;
}

Window createSwapchain(const Device& dev, GLFWwindow* handle) {
  Window win{};
  win.handle = handle;

  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev.gpu, dev.surface, &caps);

  std::uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev.gpu, dev.surface, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev.gpu, dev.surface, &formatCount, formats.data());

  VkSurfaceFormatKHR chosen = formats[0];
  for (const VkSurfaceFormatKHR& f : formats)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
      chosen = f;
      break;
    }

  win.format = chosen.format;
  win.extent = caps.currentExtent;

  VkSwapchainCreateInfoKHR swapCI{};
  swapCI.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapCI.surface          = dev.surface;
  swapCI.minImageCount    = std::max(caps.minImageCount, 2u);
  swapCI.imageFormat      = chosen.format;
  swapCI.imageColorSpace  = chosen.colorSpace;
  swapCI.imageExtent      = win.extent;
  swapCI.imageArrayLayers = 1;
  swapCI.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swapCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapCI.preTransform     = caps.currentTransform;
  swapCI.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapCI.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
  swapCI.clipped          = VK_TRUE;
  check(vkCreateSwapchainKHR(dev.device, &swapCI, nullptr, &win.swapchain), "vkCreateSwapchainKHR");

  std::uint32_t imageCount = 0;
  vkGetSwapchainImagesKHR(dev.device, win.swapchain, &imageCount, nullptr);
  win.images.resize(imageCount);
  vkGetSwapchainImagesKHR(dev.device, win.swapchain, &imageCount, win.images.data());

  VkSemaphoreCreateInfo semaCI{};
  semaCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  check(vkCreateSemaphore(dev.device, &semaCI, nullptr, &win.acquired), "vkCreateSemaphore");
  check(vkCreateSemaphore(dev.device, &semaCI, nullptr, &win.rendered), "vkCreateSemaphore");
  return win;
}

void destroySwapchain(const Device& dev, Window& win) {
  vkDestroySemaphore(dev.device, win.rendered, nullptr);
  vkDestroySemaphore(dev.device, win.acquired, nullptr);
  vkDestroySwapchainKHR(dev.device, win.swapchain, nullptr);
  glfwDestroyWindow(win.handle);
  glfwTerminate();
  win = Window{};
}

void showWindow(const Window& win) {
  glfwShowWindow(win.handle);
}

bool windowOpen(const Window& win) {
  glfwPollEvents();
  return !glfwWindowShouldClose(win.handle);
}

static void transition(VkCommandBuffer cmd,
                       VkImage image,
                       VkImageLayout from,
                       VkImageLayout to,
                       VkAccessFlags srcAccess,
                       VkAccessFlags dstAccess)
{
  VkImageMemoryBarrier barrier{};
  barrier.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask    = srcAccess;
  barrier.dstAccessMask    = dstAccess;
  barrier.oldLayout        = from;
  barrier.newLayout        = to;
  barrier.image            = image;
  barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void presentTarget(const Device& dev, const Window& win, const Target& target) {
  std::uint32_t index = 0;
  VkResult acquire = vkAcquireNextImageKHR(dev.device, win.swapchain, UINT64_MAX, win.acquired,
                                           VK_NULL_HANDLE, &index);
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) return;

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(dev.cmd, &beginInfo), "vkBeginCommandBuffer");

  transition(dev.cmd, win.images[index], VK_IMAGE_LAYOUT_UNDEFINED,
             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);

  VkImageBlit blit{};
  blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  blit.srcOffsets[1]  = { static_cast<std::int32_t>(target.width),
                          static_cast<std::int32_t>(target.height), 1 };
  blit.dstOffsets[1]  = { static_cast<std::int32_t>(win.extent.width),
                          static_cast<std::int32_t>(win.extent.height), 1 };
  vkCmdBlitImage(dev.cmd, target.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, win.images[index],
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

  transition(dev.cmd, win.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
  check(vkEndCommandBuffer(dev.cmd), "vkEndCommandBuffer");

  VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;

  VkSubmitInfo submit{};
  submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount   = 1;
  submit.pWaitSemaphores      = &win.acquired;
  submit.pWaitDstStageMask    = &wait;
  submit.commandBufferCount   = 1;
  submit.pCommandBuffers      = &dev.cmd;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores    = &win.rendered;
  check(vkQueueSubmit(dev.gfxQueue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");

  VkPresentInfoKHR present{};
  present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores    = &win.rendered;
  present.swapchainCount     = 1;
  present.pSwapchains        = &win.swapchain;
  present.pImageIndices      = &index;
  vkQueuePresentKHR(dev.gfxQueue, &present);
  vkQueueWaitIdle(dev.gfxQueue);
}
