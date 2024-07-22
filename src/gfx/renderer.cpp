#include "renderer.hpp"
#include "editor/grid.hpp"
#include "editor/window.hpp"
#include "gfx/instance.hpp"
#include "gfx/pipeline.hpp"
#include "utils/logger.hpp"
#include "utils/region.hpp"
#include "utils/unicode.hpp"
#include "utils/color.hpp"
#include "webgpu_tools/utils/webgpu.hpp"
#include <utility>
#include <vector>
#include "glm/gtx/string_cast.hpp"

using namespace wgpu;

Renderer::Renderer(const SizeHandler& sizes) {
  clearColor = {0.0, 0.0, 0.0, 1.0};

  // shared
  camera = Ortho2D(sizes.size);

  finalRenderTexture =
    RenderTexture(sizes.uiSize, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb);
  finalRenderTexture.UpdatePos(sizes.offset);

  // rect
  rectRPD = utils::RenderPassDescriptor({
    RenderPassColorAttachment{
      .loadOp = LoadOp::Clear,
      .storeOp = StoreOp::Store,
    },
  });
  rectNoClearRPD = utils::RenderPassDescriptor({
    RenderPassColorAttachment{
      .loadOp = LoadOp::Load,
      .storeOp = StoreOp::Store,
    },
  });

  // text
  textRPD = utils::RenderPassDescriptor({
    RenderPassColorAttachment{
      .loadOp = LoadOp::Load,
      .storeOp = StoreOp::Store,
    },
  });

  textMaskRPD = utils::RenderPassDescriptor({
    RenderPassColorAttachment{
      .loadOp = LoadOp::Clear,
      .storeOp = StoreOp::Store,
      .clearValue = {0.0, 0.0, 0.0, 0.0},
    },
  });

  // windows
  auto stencilTextureView =
    utils::CreateRenderTexture(ctx.device, sizes.uiFbSize, TextureFormat::Stencil8)
    .CreateView();

  windowsRPD = utils::RenderPassDescriptor(
    {
      RenderPassColorAttachment{
        .view = finalRenderTexture.textureView,
        .loadOp = LoadOp::Undefined, // set later in RenderWindows
        .storeOp = StoreOp::Store,
      },
    },
    RenderPassDepthStencilAttachment{
      .view = stencilTextureView,
      .stencilLoadOp = LoadOp::Clear,
      .stencilStoreOp = StoreOp::Store,
      .stencilClearValue = 0,
    }
  );

  // final texture
  finalRPD = utils::RenderPassDescriptor({
    RenderPassColorAttachment{
      .loadOp = LoadOp::Clear,
      .storeOp = StoreOp::Store,
    },
  });

  // cursor
  cursorData.CreateBuffers(1);

  cursorRPD = utils::RenderPassDescriptor({
    RenderPassColorAttachment{
      .loadOp = LoadOp::Load,
      .storeOp = StoreOp::Store,
    },
  });
}

void Renderer::Resize(const SizeHandler& sizes) {
  camera.Resize(sizes.size);

  finalRenderTexture =
    RenderTexture(sizes.uiSize, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb);
  finalRenderTexture.UpdatePos(sizes.offset);
  windowsRPD.cColorAttachments[0].view = finalRenderTexture.textureView;

  auto stencilTextureView =
    utils::CreateRenderTexture(ctx.device, sizes.uiFbSize, TextureFormat::Stencil8)
    .CreateView();
  windowsRPD.cDepthStencilAttachmentInfo.view = stencilTextureView;
}

void Renderer::SetClearColor(glm::vec4 color) {
  clearColor = ToWGPUColor(color);
  premultClearColor = ToWGPUColor(PremultiplyAlpha(color));
  // premultClearColor = ToWGPUColor(PremultiplyAlpha(AdjustAlpha(color)));
  linearClearColor = ToWGPUColor(ToLinear(color));
}

void Renderer::Begin() {
  commandEncoder = ctx.device.CreateCommandEncoder();
  SurfaceTexture surfaceTexture;
  ctx.surface.GetCurrentTexture(&surfaceTexture);
  nextTexture = surfaceTexture.texture;
  nextTextureView = nextTexture.CreateView();
}

void Renderer::RenderToWindow(
  Win& win, FontFamily& fontFamily, const HlTable& hlTable
) {
  // if for whatever reason (prob nvim events buggy, events not sent or offsync)
  // the grid is not the same size as the window
  // don't render
  if (win.grid.width != win.width || win.grid.height != win.height) {
    LOG_WARN(
      "RenderWindow: grid size not equal to window size for id: {}\n"
      "Sizes: grid: {}x{}, window: {}x{}",
      win.id, win.grid.width, win.grid.height, win.width, win.height
    );
    return;
  }

  // keep track of quad index after each row
  size_t rows = win.grid.lines.Size();
  std::vector<int> rectIntervals; rectIntervals.reserve(rows + 1);
  std::vector<int> textIntervals; textIntervals.reserve(rows + 1);

  auto& rectData = win.rectData;
  auto& textData = win.textData;

  rectData.ResetCounts();
  textData.ResetCounts();

  glm::vec2 textOffset(0, 0);
  const auto& defaultFont = fontFamily.DefaultFont();

  for (size_t row = 0; row < rows; row++) {
    auto& line = win.grid.lines[row];
    textOffset.x = 0;

    rectIntervals.push_back(rectData.quadCount);
    textIntervals.push_back(textData.quadCount);

    for (auto& cell : line) {
      const Highlight& hl = hlTable.at(cell.hlId);
      // don't render background if default
      if (cell.hlId != 0 && hl.background.has_value() &&
          hl.background != hlTable.at(0).background) {
        auto rectPositions = MakeRegion({0, 0}, defaultFont.charSize);

        auto background = *hl.background;
        background.a = hl.bgAlpha;
        auto& quad = rectData.NextQuad();
        for (size_t i = 0; i < 4; i++) {
          quad[i].position = textOffset + rectPositions[i];
          quad[i].color = background;
        }
      }

      if (!cell.text.empty() && cell.text != " ") {
        char32_t charcode = UTF8ToUnicode(cell.text);
        const auto& glyphInfo = fontFamily.GetGlyphInfo(charcode, hl.bold, hl.italic);

        glm::vec2 textQuadPos{
          textOffset.x + glyphInfo.bearing.x,
          textOffset.y - glyphInfo.bearing.y + defaultFont.size,
        };

        glm::vec4 foreground = GetForeground(hlTable, hl);
        auto& quad = textData.NextQuad();
        for (size_t i = 0; i < 4; i++) {
          quad[i].position = textQuadPos + glyphInfo.sizePositions[i];
          quad[i].regionCoords = glyphInfo.region[i];
          quad[i].foreground = foreground;
        }
      }

      textOffset.x += defaultFont.charSize.x;
    }

    textOffset.y += defaultFont.charSize.y;
  }

  rectIntervals.push_back(rectData.quadCount);
  textIntervals.push_back(textData.quadCount);

  rectData.WriteBuffers();
  textData.WriteBuffers();

  // gpu texture is reallocated if resized
  // old gpu texture is not referenced by texture atlas anymore
  // but still referenced by command encoder if used by previous windows
  fontFamily.textureAtlas.Update();

  auto renderInfos = win.sRenderTexture.GetRenderInfos();
  static int i = 0;
  LOG_DISABLE();
  LOG("{} -------------------", i++);

  for (auto [renderTexture, range, clearRegion] : renderInfos) {
    {
      auto& currRPD = clearRegion.has_value() ? rectNoClearRPD : rectRPD;
      currRPD.cColorAttachments[0].view = renderTexture->textureView;
      currRPD.cColorAttachments[0].clearValue = linearClearColor;
      RenderPassEncoder passEncoder = commandEncoder.BeginRenderPass(&currRPD);
      passEncoder.SetPipeline(ctx.pipeline.rectRPL);
      passEncoder.SetBindGroup(0, renderTexture->camera.viewProjBG);

      if (clearRegion.has_value()) {
        QuadRenderData<RectQuadVertex> clearData(1);
        auto region = clearRegion->Region();
        auto& quad = clearData.NextQuad();
        for (size_t i = 0; i < 4; i++) {
          quad[i].position = region[i];
          quad[i].color = ToGlmColor(clearColor);
        }
        clearData.WriteBuffers();
        clearData.Render(passEncoder);
      }

      int start = rectIntervals[range.start];
      int end = rectIntervals[range.end];
      LOG("1 - start: {}, end: {}", start, end);
      if (start != end) rectData.Render(passEncoder, start, end - start);
      passEncoder.End();
    }
    {
      textRPD.cColorAttachments[0].view = renderTexture->textureView;
      RenderPassEncoder passEncoder = commandEncoder.BeginRenderPass(&textRPD);
      passEncoder.SetPipeline(ctx.pipeline.textRPL);
      passEncoder.SetBindGroup(0, renderTexture->camera.viewProjBG);
      passEncoder.SetBindGroup(1, fontFamily.textureAtlas.fontTextureBG);
      int start = textIntervals[range.start];
      int end = textIntervals[range.end];
      LOG("2 - start: {}, end: {}", start, end);
      if (start != end) textData.Render(passEncoder, start, end - start);
      passEncoder.End();
    }
  }

  rectRPD.cColorAttachments[0].view = nullptr;
  textRPD.cColorAttachments[0].view = nullptr;
}

void Renderer::RenderCursorMask(
  const Cursor& cursor, const Win& win, FontFamily& fontFamily, const HlTable& hlTable
) {
  auto& cell = win.grid.lines[cursor.row][cursor.col];

  if (!cell.text.empty() && cell.text != " ") {
    char32_t charcode = UTF8ToUnicode(cell.text);
    const auto& hl = hlTable.at(cell.hlId);
    const auto& glyphInfo = fontFamily.GetGlyphInfo(charcode, hl.bold, hl.italic);

    glm::vec2 textQuadPos{
      glyphInfo.bearing.x,
      -glyphInfo.bearing.y + fontFamily.DefaultFont().size,
    };

    textMaskRPD.cColorAttachments[0].view = cursor.maskRenderTexture.textureView;
    RenderPassEncoder passEncoder = commandEncoder.BeginRenderPass(&textMaskRPD);
    passEncoder.SetPipeline(ctx.pipeline.textMaskRPL);
    passEncoder.SetBindGroup(0, cursor.maskRenderTexture.camera.viewProjBG);
    passEncoder.SetBindGroup(1, fontFamily.textureAtlas.fontTextureBG);
    QuadRenderData<TextMaskQuadVertex> textMaskData(1);
    auto& quad = textMaskData.NextQuad();
    for (size_t i = 0; i < 4; i++) {
      quad[i].position = textQuadPos + glyphInfo.sizePositions[i];
      quad[i].regionCoords = glyphInfo.region[i];
    }
    textMaskData.WriteBuffers();
    textMaskData.Render(passEncoder);
    passEncoder.End();

  } else {
    // just clear the texture if there's nothing
    textMaskRPD.cColorAttachments[0].view = cursor.maskRenderTexture.textureView;
    RenderPassEncoder passEncoder = commandEncoder.BeginRenderPass(&textMaskRPD);
    passEncoder.End();
  }

  textMaskRPD.cColorAttachments[0].view = nullptr;
}

void Renderer::RenderWindows(
  std::span<const Win*> windows, std::span<const Win*> floatWindows
) {
  windowsRPD.cColorAttachments[0].clearValue = linearClearColor;
  windowsRPD.cColorAttachments[0].loadOp = LoadOp::Clear;
  {
    auto passEncoder = commandEncoder.BeginRenderPass(&windowsRPD);
    passEncoder.SetPipeline(ctx.pipeline.textureNoBlendRPL);
    passEncoder.SetStencilReference(1);
    passEncoder.SetBindGroup(0, finalRenderTexture.camera.viewProjBG);
    for (const Win* win : windows) {
      win->sRenderTexture.Render(passEncoder);
    }
    passEncoder.End();
  }
  windowsRPD.cColorAttachments[0].loadOp = LoadOp::Load;
  {
    auto passEncoder = commandEncoder.BeginRenderPass(&windowsRPD);
    passEncoder.SetPipeline(ctx.pipeline.textureRPL);
    passEncoder.SetStencilReference(1);
    passEncoder.SetBindGroup(0, finalRenderTexture.camera.viewProjBG);
    for (const Win* win : floatWindows) {
      win->sRenderTexture.Render(passEncoder);
    }
    passEncoder.End();
  }
}

void Renderer::RenderFinalTexture() {
  finalRPD.cColorAttachments[0].view = nextTextureView;
  finalRPD.cColorAttachments[0].clearValue = premultClearColor;
  auto passEncoder = commandEncoder.BeginRenderPass(&finalRPD);
  passEncoder.SetPipeline(ctx.pipeline.finalTextureRPL);
  passEncoder.SetBindGroup(0, camera.viewProjBG);
  passEncoder.SetBindGroup(1, finalRenderTexture.textureBG);
  finalRenderTexture.renderData.Render(passEncoder);
  passEncoder.End();
  finalRPD.cColorAttachments[0].view = nullptr;
}

void Renderer::RenderCursor(const Cursor& cursor, const HlTable& hlTable) {
  auto attrId = cursor.modeInfo->attrId;
  const auto& hl = hlTable.at(attrId);
  auto foreground = GetForeground(hlTable, hl);
  auto background = GetBackground(hlTable, hl);
  if (attrId == 0) std::swap(foreground, background);

  cursorData.ResetCounts();
  auto& quad = cursorData.NextQuad();
  for (size_t i = 0; i < 4; i++) {
    quad[i].position = cursor.pos + cursor.corners[i];
    quad[i].foreground = foreground;
    quad[i].background = background;
  }
  cursorData.WriteBuffers();

  cursorRPD.cColorAttachments[0].view = nextTextureView;
  RenderPassEncoder passEncoder = commandEncoder.BeginRenderPass(&cursorRPD);
  passEncoder.SetPipeline(ctx.pipeline.cursorRPL);
  passEncoder.SetBindGroup(0, camera.viewProjBG);
  passEncoder.SetBindGroup(1, cursor.maskRenderTexture.camera.viewProjBG);
  passEncoder.SetBindGroup(2, cursor.maskPosBG);
  passEncoder.SetBindGroup(3, cursor.maskRenderTexture.textureBG);
  cursorData.Render(passEncoder);
  passEncoder.End();
  cursorRPD.cColorAttachments[0].view = nullptr;
}

void Renderer::End() {
  auto commandBuffer = commandEncoder.Finish();
  ctx.queue.Submit(1, &commandBuffer);
}
