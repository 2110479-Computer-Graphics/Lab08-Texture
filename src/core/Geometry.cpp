#include "core/Geometry.h"

GpuMesh uploadMesh(const Device& dev, const Mesh& mesh) {
  GpuMesh gpu{};
  gpu.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
  gpu.indexCount  = static_cast<std::uint32_t>(mesh.indices.size());

  VkDeviceSize vertexBytes = sizeof(Vertex) * mesh.vertices.size();
  VkDeviceSize indexBytes  = sizeof(std::uint32_t) * mesh.indices.size();
  gpu.vertices = createBuffer(dev, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  gpu.indices  = createBuffer(dev, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  uploadBuffer(gpu.vertices, mesh.vertices.data(), static_cast<std::size_t>(vertexBytes));
  uploadBuffer(gpu.indices, mesh.indices.data(), static_cast<std::size_t>(indexBytes));
  return gpu;
}

void destroyMesh(const Device& dev, GpuMesh& mesh) {
  destroyBuffer(dev, mesh.vertices);
  destroyBuffer(dev, mesh.indices);
  mesh = GpuMesh{};
}

VertexLayout meshLayout() {
  VertexLayout layout{};
  layout.binding(0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX)
        .attr(0, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, pos))
        .attr(1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, normal))
        .attr(2, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Vertex, uv))
        .attr(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent));
  return layout;
}

void drawMeshRange(VkCommandBuffer cmd, const GpuMesh& mesh, std::uint32_t firstIndex,
                   std::uint32_t indexCount, std::uint32_t instanceCount,
                   std::uint32_t firstInstance)
{
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.handle, &offset);
  vkCmdBindIndexBuffer(cmd, mesh.indices.handle, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, indexCount, instanceCount, firstIndex, 0, firstInstance);
}

void drawMesh(VkCommandBuffer cmd, const GpuMesh& mesh, std::uint32_t instanceCount,
              std::uint32_t firstInstance)
{
  drawMeshRange(cmd, mesh, 0, mesh.indexCount, instanceCount, firstInstance);
}

void bindInstances(VkCommandBuffer cmd, const Buffer& instances, std::uint32_t binding) {
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, binding, 1, &instances.handle, &offset);
}
