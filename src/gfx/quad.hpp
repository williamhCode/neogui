#pragma once

#include "webgpu_tools/utils/webgpu.hpp"
#include "gfx/instance.hpp"

template <class VertexType>
struct QuadRenderData {
  using Quad = std::array<VertexType, 4>;
  size_t quadCount = 0;
  size_t vertexCount = 0;
  size_t indexCount = 0;
  std::vector<Quad> quads;
  std::vector<uint32_t> indices;
  wgpu::Buffer vertexBuffer;
  wgpu::Buffer indexBuffer;

  QuadRenderData() = default;
  QuadRenderData(size_t numQuads) {
    CreateBuffers(numQuads);
  }

  void CreateBuffers(size_t numQuads) {
    quads.resize(numQuads);
    indices.resize(numQuads * 6);

    vertexBuffer =
      wgpu::utils::CreateVertexBuffer(ctx.device, sizeof(VertexType) * 4 * numQuads);
    indexBuffer =
      wgpu::utils::CreateIndexBuffer(ctx.device, sizeof(uint32_t) * 6 * numQuads);
  }

  void ResetCounts() {
    quadCount = 0;
    vertexCount = 0;
    indexCount = 0;
  }

  Quad& NextQuad() {
    assert(quadCount < quads.size());

    auto& quad = quads[quadCount];

    indices[indexCount + 0] = vertexCount + 0;
    indices[indexCount + 1] = vertexCount + 1;
    indices[indexCount + 2] = vertexCount + 2;
    indices[indexCount + 3] = vertexCount + 2;
    indices[indexCount + 4] = vertexCount + 3;
    indices[indexCount + 5] = vertexCount + 0;

    quadCount++;
    vertexCount += 4;
    indexCount += 6;

    return quad;
  }

  void WriteBuffers() {
    ctx.queue.WriteBuffer(
      vertexBuffer, 0, quads.data(), sizeof(VertexType) * vertexCount
    );
    ctx.queue.WriteBuffer(
      indexBuffer, 0, indices.data(), sizeof(uint32_t) * indexCount
    );
  }

  // option offset and size in unit of quads
  void Render(
    const wgpu::RenderPassEncoder& passEncoder, uint64_t offset = 0, uint64_t size = 0
  ) const {
    assert(offset <= quadCount);
    assert(size <= quadCount);
    if (size == 0) size = quadCount;

    passEncoder.SetVertexBuffer(0, vertexBuffer);

    auto indexStride = sizeof(uint32_t) * 6;
    passEncoder.SetIndexBuffer(
      indexBuffer, wgpu::IndexFormat::Uint32, offset * indexStride, size * indexStride
    );

    passEncoder.DrawIndexed(size * 6);
  }
};
