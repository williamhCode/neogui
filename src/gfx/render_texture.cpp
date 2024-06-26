#include "render_texture.hpp"

#include "webgpu_tools/utils/webgpu.hpp"
#include "utils/region.hpp"
#include "gfx/instance.hpp"

using namespace wgpu;

RenderTexture::RenderTexture(
  glm::vec2 _size, float dpiScale, wgpu::TextureFormat format, const void* data
)
    : size(_size) {
  camera = Ortho2D(size);

  auto fbSize = size * dpiScale;
  texture =
    utils::CreateRenderTexture(ctx.device, Extent3D(fbSize.x, fbSize.y), format, data);
  textureView = texture.CreateView();

  auto textureSampler = ctx.device.CreateSampler(ToPtr(SamplerDescriptor{
    .addressModeU = AddressMode::ClampToEdge,
    .addressModeV = AddressMode::ClampToEdge,
    .magFilter = FilterMode::Nearest,
    .minFilter = FilterMode::Nearest,
  }));

  textureBG = utils::MakeBindGroup(
    ctx.device, ctx.pipeline.textureBGL,
    {
      {0, textureView},
      {1, textureSampler},
    }
  );

  renderData.CreateBuffers(1);
}

void RenderTexture::UpdatePos(glm::vec2 pos, RegionHandle* region) {
  renderData.ResetCounts();

  Region positions;
  Region uvs;

  if (region == nullptr) {
    positions = MakeRegion(pos, size);
    uvs = MakeRegion({0, 0}, {1, 1});
  } else {
    positions = MakeRegion(pos, region->size);
    region->pos /= size, region->size /= size;
    uvs = region->Get();
  }

  for (size_t i = 0; i < 4; i++) {
    auto& vertex = renderData.CurrQuad()[i];
    vertex.position = positions[i];
    vertex.uv = uvs[i];
  }
  renderData.Increment();
  renderData.WriteBuffers();
}
