#include "window.hpp"
#include "glm/ext/vector_float2.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cstdlib>
#include <utility>

using namespace wgpu;

void WinManager::InitRenderData(Win& win) {
  auto pos = glm::vec2(win.startCol, win.startRow) * sizes->charSize;
  auto size = glm::vec2(win.width, win.height) * sizes->charSize;

  const size_t maxTextQuads = win.width * win.height;
  win.rectData.CreateBuffers(maxTextQuads);
  win.textData.CreateBuffers(maxTextQuads);

  win.sRenderTexture = ScrollableRenderTexture(size, sizes->dpiScale, sizes->charSize);
  win.sRenderTexture.UpdatePos(pos);

  // used if hiding window removes window completely
  // win.grid.dirty = true;

  win.pos = pos;
  win.size = size;
}

void WinManager::UpdateRenderData(Win& win) {
  auto pos = glm::vec2(win.startCol, win.startRow) * sizes->charSize;
  auto size = glm::vec2(win.width, win.height) * sizes->charSize;

  bool posChanged = pos != win.pos;
  bool sizeChanged = size != win.size;
  if (!posChanged && !sizeChanged) {
    return;
  }

  if (sizeChanged) {
    const size_t maxTextQuads = win.width * win.height;
    win.rectData.CreateBuffers(maxTextQuads);
    win.textData.CreateBuffers(maxTextQuads);

    win.sRenderTexture = ScrollableRenderTexture(size, sizes->dpiScale, sizes->charSize);
  }
  win.sRenderTexture.UpdatePos(pos);

  // used if hiding window removes window completely
  // win.grid.dirty = true;

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
  auto it = windows.find(e.grid);
  if (it == windows.end()) {
    LOG_ERR("WinManager::Hide: window {} not found", e.grid);
    return;
  }
  auto& win = it->second;
  win.hidden = true;

  // save memory when tabs get hidden
  // NOTE: as of now ViewportMargins event is not always sent when switching tabs (so removing makes it margins weird)
  // auto removed = windows.erase(e.grid);
  // if (removed == 0) {
  //   LOG_ERR("WinManager::Hide: window {} not found", e.grid);
  // }
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
    // LOG_ERR("WinManager::Viewport: window {} not found", e.grid);
    return;
  }
  auto& win = it->second;

  bool shouldScroll =              //
    std::abs(e.scrollDelta) > 0 && //
    std::abs(e.scrollDelta) <= win.height - (win.margins.top + win.margins.bottom);

  // LOG_INFO("WinManager::Viewport: grid {} scrollDelta {} shouldScroll {}", e.grid,

  if (!shouldScroll) return;
  float scrollDist = e.scrollDelta * sizes->charSize.y;
  win.sRenderTexture.UpdateViewport(scrollDist);
}

void WinManager::UpdateScrolling(float dt) {
  for (auto& [id, win] : windows) {
    if (!win.sRenderTexture.scrolling) continue;
    win.sRenderTexture.UpdateScrolling(dt);
    dirty = true;
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

  win.sRenderTexture.UpdateMargins(win.margins);
}

void WinManager::Extmark(const WinExtmark& e) {
}

MouseInfo WinManager::GetMouseInfo(glm::vec2 mousePos) {
  mousePos -= sizes->offset;
  int globalRow = mousePos.y / sizes->charSize.y;
  int globalCol = mousePos.x / sizes->charSize.x;

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

    if (globalRow >= top && globalRow < bottom && globalCol >= left &&
        globalCol < right) {
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

  mousePos -= sizes->offset;
  int globalRow = mousePos.y / sizes->charSize.y;
  int globalCol = mousePos.x / sizes->charSize.x;

  int row = std::max(globalRow - win.startRow, 0);
  int col = std::max(globalCol - win.startCol, 0);

  return {grid, row, col};
}

Win* WinManager::GetWin(int id) {
  auto it = windows.find(id);
  if (it == windows.end()) return nullptr;
  return &it->second;
}

Win* WinManager::GetMsgWin() {
  return GetWin(msgWinId);
}
