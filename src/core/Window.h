#ifndef __WINDOW_H_INCLUDED__
#define __WINDOW_H_INCLUDED__

#include "core/Target.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

struct Window {
  GLFWwindow*              handle    = nullptr;
  VkSwapchainKHR           swapchain = VK_NULL_HANDLE;
  std::vector<VkImage>     images;
  VkExtent2D               extent{};
  VkFormat                 format   = VK_FORMAT_UNDEFINED;
  VkSemaphore              acquired = VK_NULL_HANDLE;
  VkSemaphore              rendered = VK_NULL_HANDLE;
};

GLFWwindow* createHiddenWindow(const char* title, std::uint32_t width, std::uint32_t height);

Window createSwapchain(const Device& dev, GLFWwindow* handle);
void   destroySwapchain(const Device& dev, Window& win);

void showWindow(const Window& win);
bool windowOpen(const Window& win);
void presentTarget(const Device& dev, const Window& win, const Target& target);

#endif
