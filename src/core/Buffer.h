#ifndef __BUFFER_H_INCLUDED__
#define __BUFFER_H_INCLUDED__

#include "core/Device.h"

struct Buffer {
  VkBuffer       handle = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void*          mapped = nullptr;
  VkDeviceSize   size   = 0;
};

Buffer createBuffer(const Device& dev, VkDeviceSize size, VkBufferUsageFlags usage);
void   destroyBuffer(const Device& dev, Buffer& buf);
void   uploadBuffer(const Buffer& buf, const void* data, std::size_t bytes);

#endif
