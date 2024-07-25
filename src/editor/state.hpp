#pragma once

#include "editor/ui_options.hpp"
#include "editor/grid.hpp"
#include "editor/window.hpp"
#include "editor/highlight.hpp"
#include "editor/cursor.hpp"
#include "editor/font.hpp"
#include <vector>

// All state information that gets parsed from ui events.
// Editor refers to neovim, so per neovim instance, there is an editor state.
struct EditorState {
  UiOptions uiOptions;
  GridManager gridManager;
  WinManager winManager;
  HlTable hlTable;
  Cursor cursor;
  std::vector<CursorMode> cursorModes;
  FontFamily fontFamily;
  // std::map<int, std::string> hlGroupTable;

  void Init(const SizeHandler& sizes);
};

// returns true if there were events processed
bool ParseEditorState(UiEvents& uiEvents, EditorState& editorState);
