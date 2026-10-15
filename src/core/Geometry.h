#ifndef __GEOMETRY_H_INCLUDED__
#define __GEOMETRY_H_INCLUDED__

#include "core/Pipeline.h"
#include "utils/Mesh.h"

struct GpuMesh {
  Buffer        vertices;
  Buffer        indices;
  std::uint32_t vertexCount = 0;
  std::uint32_t indexCount  = 0;
};

GpuMesh uploadMesh(const Device& dev, const Mesh& mesh);
void    destroyMesh(const Device& dev, GpuMesh& mesh);

VertexLayout meshLayout();

void drawMesh(VkCommandBuffer cmd, const GpuMesh& mesh, std::uint32_t instanceCount = 1,
              std::uint32_t firstInstance = 0);
void drawMeshRange(VkCommandBuffer cmd, const GpuMesh& mesh, std::uint32_t firstIndex,
                   std::uint32_t indexCount, std::uint32_t instanceCount = 1,
                   std::uint32_t firstInstance = 0);

void bindInstances(VkCommandBuffer cmd, const Buffer& instances, std::uint32_t binding = 1);

#endif
