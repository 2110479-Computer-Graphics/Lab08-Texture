#include "core/Buffer.h"

#include <cstring>

Buffer createBuffer(const Device& dev, VkDeviceSize size, VkBufferUsageFlags usage) {
  Buffer buf{};
  buf.size = size;

  VkBufferCreateInfo bufCI{};
  bufCI.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufCI.size        = size;
  bufCI.usage       = usage;
  bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(dev.device, &bufCI, nullptr, &buf.handle), "vkCreateBuffer");

  VkMemoryRequirements memReq;
  vkGetBufferMemoryRequirements(dev.device, buf.handle, &memReq);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize  = memReq.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      dev, memReq.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkAllocateMemory(dev.device, &allocInfo, nullptr, &buf.memory), "vkAllocateMemory");

  check(vkBindBufferMemory(dev.device, buf.handle, buf.memory, 0), "vkBindBufferMemory");
  check(vkMapMemory(dev.device, buf.memory, 0, size, 0, &buf.mapped), "vkMapMemory");
  return buf;
}

void destroyBuffer(const Device& dev, Buffer& buf) {
  if (buf.handle == VK_NULL_HANDLE) return;
  vkUnmapMemory(dev.device, buf.memory);
  vkDestroyBuffer(dev.device, buf.handle, nullptr);
  vkFreeMemory(dev.device, buf.memory, nullptr);
  buf = Buffer{};
}

void uploadBuffer(const Buffer& buf, const void* data, std::size_t bytes) {
  std::memcpy(buf.mapped, data, bytes);
}
