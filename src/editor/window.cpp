#include "window.hpp"
#include "gfx/instance.hpp"
#include "glm/common.hpp"
#include "glm/exponential.hpp"
#include "glm/ext/vector_float2.hpp"
#include "webgpu_tools/utils/webgpu.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cstdlib>
#include <utility>

using namespace wgpu;

bool Margins::Empty() const {
  return top == 0 && bottom == 0 && left == 0 && right == 0;
}

FMargins Margins::ToFloat(glm::vec2 size) const {
  return {
    top * size.y,
    bottom * size.y,
    left * size.x,
    right * size.x,
  };
}

void WinManager::InitRenderData(Win& win) {
  auto pos = glm::vec2(win.startCol, win.startRow) * sizes.charSize;
  auto size = glm::vec2(win.width, win.height) * sizes.charSize;

  // std::vector<ColorBytes> colorData(
  //   size.x * size.y * sizes.dpiScale * sizes.dpiScale, clearColor
  // );
  // win.renderTexture =
  //   RenderTexture(size, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb, colorData.data());
  win.renderTexture = RenderTexture(size, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb);
  win.renderTexture.UpdatePos(pos);

  if (win.id != 1 && win.id != msgWinId) {
    win.prevRenderTexture =
      RenderTexture(size, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb);
    win.prevRenderTexture.UpdatePos(pos);
  }

  auto fbSize = size * sizes.dpiScale;
  Extent3D maskSize(fbSize.x, fbSize.y);
  win.maskTextureView =
    utils::CreateRenderTexture(ctx.device, maskSize, TextureFormat::R8Unorm)
      .CreateView();

  auto maskPos = pos * sizes.dpiScale;
  win.maskPosBuffer =
    utils::CreateUniformBuffer(ctx.device, sizeof(glm::vec2), &maskPos);

  win.maskBG = utils::MakeBindGroup(
    ctx.device, ctx.pipeline.maskBGL,
    {
      {0, win.maskTextureView},
      {1, win.maskPosBuffer},
    }
  );

  const size_t maxTextQuads = win.width * win.height;
  win.rectData.CreateBuffers(maxTextQuads);
  win.textData.CreateBuffers(maxTextQuads);

  win.marginsData.CreateBuffers(4);

  win.grid.dirty = true;

  win.pos = pos;
  win.size = size;
}

void WinManager::UpdateRenderData(Win& win) {
  auto pos = glm::vec2(win.startCol, win.startRow) * sizes.charSize;
  auto size = glm::vec2(win.width, win.height) * sizes.charSize;

  bool posChanged = pos != win.pos;
  bool sizeChanged = size != win.size;
  if (!posChanged && !sizeChanged) {
    return;
  }

  if (sizeChanged) {
    // std::vector<ColorBytes> colorData(
    //   size.x * size.y * sizes.dpiScale * sizes.dpiScale, clearColor
    // );
    // win.renderTexture =
    //   RenderTexture(size, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb,
    //   colorData.data());
    win.renderTexture = RenderTexture(size, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb);
    win.renderTexture.UpdatePos(pos);

    if (win.id != 1 && win.id != msgWinId) {
      win.prevRenderTexture =
        RenderTexture(size, sizes.dpiScale, TextureFormat::RGBA8UnormSrgb);
      win.prevRenderTexture.UpdatePos(pos);
    }
  } else {
    win.renderTexture.UpdatePos(pos);
    if (win.id != 1 && win.id != msgWinId) {
      win.prevRenderTexture.UpdatePos(pos);
    }
  }

  if (sizeChanged) {
    auto fbSize = size * sizes.dpiScale;
    win.maskTextureView =
      utils::CreateRenderTexture(
        ctx.device, Extent3D(fbSize.x, fbSize.y), TextureFormat::R8Unorm
      )
        .CreateView();

    win.maskBG = utils::MakeBindGroup(
      ctx.device, ctx.pipeline.maskBGL,
      {
        {0, win.maskTextureView},
        {1, win.maskPosBuffer},
      }
    );

    win.grid.dirty = true;
  }

  if (posChanged) {
    auto maskPos = pos * sizes.dpiScale;
    ctx.queue.WriteBuffer(win.maskPosBuffer, 0, &maskPos, sizeof(glm::vec2));
  }

  if (sizeChanged) {
    const size_t maxTextQuads = win.width * win.height;
    win.rectData.CreateBuffers(maxTextQuads);
    win.textData.CreateBuffers(maxTextQuads);
  }

  win.pos = pos;
  win.size = size;
}

void WinManager::Pos(const WinPos& e) {
  auto [it, first] = windows.try_emplace(
    e.grid, Win{.id = e.grid, .grid = gridManager->grids.at(e.grid)}
  );
  auto& win = it->second;

  win.startRow = e.startRow;
  win.startCol = e.startCol;

  win.width = e.width;
  win.height = e.height;

  win.hidden = false;

  if (first) {
    InitRenderData(win);
  } else {
    UpdateRenderData(win);
  }
}

void WinManager::FloatPos(const WinFloatPos& e) {
  auto [winIt, first] = windows.try_emplace(
    e.grid, Win{.id = e.grid, .grid = gridManager->grids.at(e.grid)}
  );
  auto& win = winIt->second;

  win.width = win.grid.width;
  win.height = win.grid.height;

  win.hidden = false;

  auto anchorIt = windows.find(e.anchorGrid);
  if (anchorIt == windows.end()) {
    LOG_ERR("WinManager::FloatPos: anchor grid {} not found", e.anchorGrid);
    return;
  }
  auto& anchorWin = anchorIt->second;

  auto north = anchorWin.startRow + e.anchorRow;
  auto south = anchorWin.startRow + e.anchorRow - win.height;
  auto west = anchorWin.startCol + e.anchorCol;
  auto east = anchorWin.startCol + e.anchorCol - win.width;
  if (e.anchor == "NW") {
    win.startRow = north;
    win.startCol = west;
  } else if (e.anchor == "NE") {
    win.startRow = north;
    win.startCol = east;
  } else if (e.anchor == "SW") {
    win.startRow = south;
    win.startCol = west;
  } else if (e.anchor == "SE") {
    win.startRow = south;
    win.startCol = east;
  } else {
    LOG_WARN("WinManager::FloatPos: unknown anchor {}", e.anchor);
  }

  win.floatData = FloatData{
    .focusable = e.focusable,
    .zindex = e.zindex,
  };

  if (first) {
    InitRenderData(win);
  } else {
    UpdateRenderData(win);
  }
}

void WinManager::ExternalPos(const WinExternalPos& e) {
}

void WinManager::Hide(const WinHide& e) {
  // auto it = windows.find(e.grid);
  // if (it == windows.end()) {
  //   LOG_ERR("WinManager::Hide: window {} not found", e.grid);
  //   return;
  // }
  // auto& win = it->second;
  // win.hidden = true;

  // save memory when tabs get hidden
  auto removed = windows.erase(e.grid);
  if (removed == 0) {
    LOG_ERR("WinManager::Hide: window {} not found", e.grid);
  }
}

void WinManager::Close(const WinClose& e) {
  auto removed = windows.erase(e.grid);
  if (removed == 0) {
    // see editor/state.cpp GridDestroy
    // LOG_WARN("WinManager::Close: window {} not found - ignore due to nvim bug",
    // e.grid);
  }
}

void WinManager::MsgSet(const MsgSetPos& e) {
  auto [winIt, first] = windows.try_emplace(
    e.grid, Win{.id = e.grid, .grid = gridManager->grids.at(e.grid)}
  );
  auto& win = winIt->second;

  win.startRow = e.row;
  win.startCol = 0;

  win.width = win.grid.width;
  win.height = win.grid.height;

  win.hidden = false;

  msgWinId = e.grid;

  if (first) {
    InitRenderData(win);
  } else {
    UpdateRenderData(win);
  }
}

void WinManager::Viewport(const WinViewport& e) {
  auto it = windows.find(e.grid);
  if (it == windows.end()) {
    LOG_ERR("WinManager::Viewport: window {} not found", e.grid);
    return;
  }
  auto& win = it->second;

  bool shouldScroll =              //
    std::abs(e.scrollDelta) > 0 && //
    std::abs(e.scrollDelta) <= win.height - (win.margins.top + win.margins.bottom);

  // LOG_INFO("WinManager::Viewport: grid {} scrollDelta {} shouldScroll {}", e.grid,
  // e.scrollDelta,
  //          shouldScroll);
  if (shouldScroll) {
    win.scrolling = true;
    win.scrollDist = e.scrollDelta * sizes.charSize.y;
    win.scrollElapsed = 0;

    std::swap(win.prevRenderTexture, win.renderTexture);
    // win.hasPrevRender = true;
  }
}

void WinManager::UpdateScrolling(float dt) {
  for (auto& [id, win] : windows) {
    if (!win.scrolling) continue;

    win.scrollElapsed += dt;
    dirty = true;

    auto pos = win.pos;

    if (win.scrollElapsed >= win.scrollTime) {
      win.scrolling = false;
      win.scrollElapsed = 0;

      win.renderTexture.UpdatePos(pos);

      auto maskPos = pos * sizes.dpiScale;
      ctx.queue.WriteBuffer(win.maskPosBuffer, 0, &maskPos, sizeof(glm::vec2));

      win.hasPrevRender = true;

    } else {
      auto size = win.size;
      auto& margins = win.fmargins;

      float t = win.scrollElapsed / win.scrollTime;
      float x = glm::pow(t, 1 / 2.0f);
      win.scrollCurr =
        glm::sign(win.scrollDist) * glm::mix(0.0f, glm::abs(win.scrollDist), x);
      pos.y -= win.scrollCurr;

      float scrollCurrAbs = glm::abs(win.scrollCurr);
      float scrollDistAbs = glm::abs(win.scrollDist);
      float innerWidth = size.x - margins.left - margins.right;
      float innerHeight = size.y - margins.top - margins.bottom;
      float toScroll = scrollDistAbs - scrollCurrAbs;
      bool scrollPositive = win.scrollDist > 0;

      RegionHandle prevRegion{
        .pos{
          margins.left,
          scrollPositive ? margins.top + scrollCurrAbs
                         : size.y - margins.bottom - scrollDistAbs,
        },
        .size{innerWidth, toScroll},
      };
      win.prevRenderTexture.UpdatePos(pos + prevRegion.pos, &prevRegion);

      RegionHandle region{
        .pos{margins.left, scrollPositive ? margins.top : margins.top + toScroll},
        .size{innerWidth, innerHeight - toScroll},
      };
      pos += glm::vec2(0, win.scrollDist);
      win.renderTexture.UpdatePos(pos + region.pos, &region);

      auto maskPos = pos * sizes.dpiScale;
      ctx.queue.WriteBuffer(win.maskPosBuffer, 0, &maskPos, sizeof(glm::vec2));
    }
  }
}

void WinManager::ViewportMargins(const WinViewportMargins& e) {
  auto it = windows.find(e.grid);
  if (it == windows.end()) {
    // LOG_ERR("WinManager::ViewportMargins: window {} not found", e.grid);
    return;
  }
  auto& win = it->second;

  win.margins.top = e.top;
  win.margins.bottom = e.bottom;
  win.margins.left = e.left;
  win.margins.right = e.right;

  win.fmargins = win.margins.ToFloat(sizes.charSize);

  win.marginsData.ResetCounts();

  auto SetData = [&](glm::vec2 pos, glm::vec2 size) {
    auto positions = MakeRegion(pos, size);
    auto uvs = MakeRegion(pos, size / win.size);

    for (size_t i = 0; i < 4; i++) {
      auto& vertex = win.marginsData.CurrQuad()[i];
      vertex.position = win.pos + positions[i];
      vertex.uv = uvs[i];
    }
    win.marginsData.Increment();
  };

  if (win.margins.top != 0) {
    SetData({0, 0}, {win.size.x, win.fmargins.top});
  }
  if (win.margins.bottom != 0) {
    SetData({0, win.size.y - win.fmargins.bottom}, {win.size.x, win.fmargins.bottom});
  }
  if (win.margins.left != 0) {
    SetData(
      {0, win.fmargins.top},
      {win.fmargins.left, win.size.y - win.fmargins.top - win.fmargins.bottom}
    );
  }
  if (win.margins.right != 0) {
    SetData(
      {win.size.x - win.fmargins.right, win.fmargins.top},
      {win.fmargins.right, win.size.y - win.fmargins.top - win.fmargins.bottom}
    );
  }
  win.marginsData.WriteBuffers();
}

void WinManager::Extmark(const WinExtmark& e) {
}

Win* WinManager::GetActiveWin() {
  auto it = windows.find(activeWinId);
  if (it == windows.end()) return nullptr;
  return &it->second;
}

MouseInfo WinManager::GetMouseInfo(glm::vec2 mousePos) {
  mousePos -= sizes.offset;
  int globalRow = mousePos.y / sizes.charSize.y;
  int globalCol = mousePos.x / sizes.charSize.x;

  std::vector<std::pair<int, const Win*>> sortedWins;
  for (auto& [id, win] : windows) {
    if (win.hidden || id == 1) continue;
    sortedWins.emplace_back(id, &win);
  }

  std::ranges::sort(sortedWins, [](const auto& a, const auto& b) {
    return a.second->floatData.value_or(FloatData{.zindex = 0}).zindex >
           b.second->floatData.value_or(FloatData{.zindex = 0}).zindex;
  });

  int grid = 1; // default grid number
  for (auto [id, win] : sortedWins) {
    if (win->hidden || id == 1 || (win->floatData && !win->floatData->focusable)) {
      continue;
    }

    int top = win->startRow;
    int bottom = win->startRow + win->height;
    int left = win->startCol;
    int right = win->startCol + win->width;

    if (globalRow >= top && globalRow < bottom && globalCol >= left && globalCol < right) {
      grid = id;
      break;
    }
  }

  auto& win = windows.at(grid);
  int row = std::max(globalRow - win.startRow, 0);
  int col = std::max(globalCol - win.startCol, 0);

  return {grid, row, col};
}

MouseInfo WinManager::GetMouseInfo(int grid, glm::vec2 mousePos) {
  auto& win = windows.at(grid);

  mousePos -= sizes.offset;
  int globalRow = mousePos.y / sizes.charSize.y;
  int globalCol = mousePos.x / sizes.charSize.x;

  int row = std::max(globalRow - win.startRow, 0);
  int col = std::max(globalCol - win.startCol, 0);

  return {grid, row, col};
}
