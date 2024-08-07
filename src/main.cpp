#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include "app/size.hpp"
#include "app/input.hpp"
#include "app/sdl_window.hpp"
#include "app/sdl_event.hpp"
#include "app/options.hpp"
#include "editor/grid.hpp"
#include "editor/highlight.hpp"
#include "editor/state.hpp"
#include "editor/font.hpp"
#include "gfx/instance.hpp"
#include "gfx/render_texture.hpp"
#include "gfx/renderer.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/gtx/string_cast.hpp"
#include "nvim/events/ui.hpp"
#include "nvim/events/user.hpp"
#include "session/manager.hpp"
#include "utils/clock.hpp"
#include "utils/logger.hpp"
#include "utils/timer.hpp"
#include "session/state.hpp"

#include <boost/core/demangle.hpp>
#include <algorithm>
#include <vector>
#include <atomic>
#include <iostream>
#include <format>
#include <chrono>

using namespace wgpu;
using namespace std::chrono_literals;
using namespace std::chrono;

const WGPUContext& ctx = sdl::Window::_ctx;

int main() {
  if (SDL_Init(SDL_INIT_VIDEO)) {
    LOG_ERR("Unable to initialize SDL: {}", SDL_GetError());
    return 1;
  }

  if (FtInit()) {
    LOG_ERR("Unable to initialize freetype");
    return 1;
  }

  try {
    // init variables ---------------------
    Options options{};
    sdl::Window window;
    SizeHandler sizes{};
    Renderer renderer;
    InputHandler input;

    SessionManager sessionManager(SpawnMode::Child, options, window, sizes, renderer, input);
    sessionManager.New();
    SessionState* session = sessionManager.CurrSession();
    Nvim* nvim = &session->nvim;
    EditorState* editorState = &session->editorState;

    // main loop -----------------------------------
    std::atomic_bool exitWindow = false;
    TSQueue<SDL_Event> resizeEvents;
    TSQueue<SDL_Event> sdlEvents;

    // int frameCount = 0;

    std::thread renderThread([&] {
      bool windowFocused = true;
      bool idle = false;
      float idleElasped = 0;

      Clock clock;
      // Timer timer(10);

      while (!exitWindow) {
        float targetFps = options.window.vsync && !idle ? 0 : options.maxFps;
        float dt = clock.Tick(targetFps);

        // frameCount++;
        // if (frameCount % 60 == 0) {
        //   frameCount = 0;
        //   auto fps = clock.GetFps();
        //   auto fpsStr = std::format("fps: {:.2f}", fps);
        //   std::cout << '\r' << fpsStr << std::string(10, ' ') << std::flush;
        // }

        // timer.Start();

        // events -------------------------------------------
        // nvim events
        if (sessionManager.ShouldQuit()) {
          SDL_Event quitEvent{.type = SDL_EVENT_QUIT};
          SDL_PushEvent(&quitEvent);
          break;
        };
        session = sessionManager.CurrSession();
        nvim = &session->nvim;
        editorState = &session->editorState;

        LOG_DISABLE();
        ProcessUserEvents(*nvim->client, sessionManager);
        LOG_ENABLE();

        // LOG_DISABLE();
        ParseUiEvents(*nvim->client, nvim->uiEvents);
        // LOG_ENABLE();

        LOG_DISABLE();
        if (ParseEditorState(nvim->uiEvents, session->editorState)) {
          idle = false;
          idleElasped = 0;
        }
        LOG_ENABLE();

        // sdl events
        while (!sdlEvents.Empty()) {
          auto& event = sdlEvents.Front();
          switch (event.type) {
             case SDL_EVENT_WINDOW_FOCUS_GAINED:
              windowFocused = true;
              idle = false;
              idleElasped = 0;
              editorState->cursor.blinkState = BlinkState::Wait;
              editorState->cursor.blinkElasped = 0;
              break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
              windowFocused = false;
              break;
          }
          sdlEvents.Pop();
        }

        while (!resizeEvents.Empty()) {
          // only process the last 2 resize events
          if (resizeEvents.Size() <= 2) {
            auto& event = resizeEvents.Front();
            switch (event.type) {
              case SDL_EVENT_WINDOW_RESIZED:
                window.size = {event.window.data1, event.window.data2};
                break;
              case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                window.fbSize = {event.window.data1, event.window.data2};

                float prevDpiScale = window.dpiScale;
                window.dpiScale = window.fbSize.x / window.size.x;
                bool dpiChanged = prevDpiScale != window.dpiScale;

                if (dpiChanged) {
                  editorState->fontFamily.ChangeDpiScale(window.dpiScale);
                }

                auto uiFbSize = sizes.uiFbSize;
                sizes.UpdateSizes(
                  window.size, window.dpiScale,
                  editorState->fontFamily.DefaultFont().charSize,
                  options.margins
                );

                if (dpiChanged) {
                  editorState->cursor.Init(sizes.charSize, sizes.dpiScale);
                  // force nvim to resend all events to update dpiScale
                  nvim->UiTryResize(sizes.uiWidth + 1, sizes.uiHeight);
                }

                sdl::Window::_ctx.Resize(sizes.fbSize);

                if (uiFbSize == sizes.uiFbSize) {
                  renderer.camera.Resize(sizes.size);
                  renderer.finalRenderTexture.UpdatePos(sizes.offset);

                } else {
                  renderer.Resize(sizes);
                  nvim->UiTryResize(sizes.uiWidth, sizes.uiHeight);
                }
                break;
              }
            }
          }
          resizeEvents.Pop();
        }

        // update --------------------------------------------
        editorState->winManager.UpdateScrolling(dt);

        auto& cursor = editorState->cursor;
        const auto* activeWin = editorState->winManager.GetWin(cursor.grid);
        if (activeWin) {
          auto cursorPos =
            glm::vec2{
              activeWin->startCol + cursor.col,
              activeWin->startRow + cursor.row,
            } *
            sizes.charSize +
            sizes.offset;

          const auto& winTex = activeWin->sRenderTexture;
          auto scrollOffset =
            winTex.scrolling
            ? glm::vec2(0, (winTex.scrollDist - winTex.scrollCurr))
            : glm::vec2(0);

          auto finalPos = cursorPos + scrollOffset;
          if (cursor.SetDestPos(finalPos)) {
            SDL_Rect rect(finalPos.x, finalPos.y, sizes.charSize.x, sizes.charSize.y);
            SDL_SetTextInputArea(window.Get(), &rect, 0);
          }
        }
        editorState->cursor.Update(dt);

        // render ----------------------------------------------
        if (auto hlIter = editorState->hlTable.find(0);
            hlIter != editorState->hlTable.end()) {
          auto color = hlIter->second.background.value();
          renderer.SetClearColor(color);
        }

        if (idle) continue;
        idleElasped += dt;
        if (idleElasped >= options.cursorIdleTime) {
          idle = true;
          editorState->cursor.blinkState = BlinkState::On;
        }

        if (!windowFocused) {
          editorState->cursor.blinkState = BlinkState::On;
        }

        renderer.Begin();

        bool renderWindows = session->reattached;
        for (auto& [id, win] : editorState->winManager.windows) {
          if (win.grid.dirty) {
            renderer.RenderToWindow(
              win, editorState->fontFamily, editorState->hlTable
            );
            win.grid.dirty = false;
            renderWindows = true;
          }
        }

        if (editorState->cursor.dirty && activeWin != nullptr) {
          renderer.RenderCursorMask(
            *activeWin, editorState->cursor, editorState->fontFamily,
            editorState->hlTable
          );
          editorState->cursor.dirty = false;
        }

        if (renderWindows || editorState->winManager.dirty) {
          editorState->winManager.dirty = false;

          std::vector<const Win*> windows;
          if (const auto* msgWin = editorState->winManager.GetMsgWin()) {
            windows.push_back(msgWin);
          }
          std::vector<const Win*> floatWindows;
          for (auto& [id, win] : editorState->winManager.windows) {
            if (id == 1 || id == editorState->winManager.msgWinId ||
                win.hidden) {
              continue;
            }
            if (win.IsFloating()) {
              floatWindows.push_back(&win);
            } else {
              windows.push_back(&win);
            }
          }
          if (auto winIt = editorState->winManager.windows.find(1);
              winIt != editorState->winManager.windows.end()) {
            windows.push_back(&winIt->second);
          }

          // sort floating windows by zindex
          std::ranges::sort(floatWindows, [](const Win* win, const Win* other) {
            return win->floatData->zindex > other->floatData->zindex;
          });

          renderer.RenderWindows(windows, floatWindows);
          // switch to current texture only after rendering to it
          if (renderer.prevFinalRenderTexture.texture) {
            renderer.prevFinalRenderTexture = {};
          }
          // reset reattached flag after rendering
          session->reattached = false;
        }

        renderer.RenderFinalTexture();

        if (editorState->cursor.ShouldRender()) {
          renderer.RenderCursor(
            editorState->cursor, editorState->hlTable
          );
        }

        renderer.End();

        ctx.surface.Present();
        ctx.device.Tick();

        // timer.End();
        // auto avgDuration = duration_cast<microseconds>(timer.GetAverageDuration());
        // std::cout << '\r' << avgDuration << std::string(10, ' ') << std::flush;
      }
    });

    // event loop --------------------------------
    SDL_StartTextInput(window.Get());

    // resize handling
    sdl::AddEventWatch([&](SDL_Event& event) {
      switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
          resizeEvents.Push(event);
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          resizeEvents.Push(event);
          break;
      }
    });

    SDL_Event event;
    while (!exitWindow) {
      auto success = SDL_WaitEvent(&event);
      if (!success) {
        LOG_ERR("SDL_WaitEvent error: {}", SDL_GetError());
      }

      switch (event.type) {
        case SDL_EVENT_QUIT:
          LOG("exit window");
          exitWindow = true;
          break;

        // keyboard handling ----------------------
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
          input.HandleKeyboard(event.key);
          // sdlEvents.Push(event);
          break;

        case SDL_EVENT_TEXT_EDITING:
          break;
        case SDL_EVENT_TEXT_INPUT:
          input.HandleTextInput(event.text);
          break;

        // mouse handling ------------------------
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
          input.HandleMouseButton(event.button);
          break;
        case SDL_EVENT_MOUSE_MOTION:
          input.HandleMouseMotion(event.motion);
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          input.HandleMouseWheel(event.wheel);
          break;

        // window handling -----------------------
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          sdlEvents.Push(event);
          break;
      }
    }

    renderThread.join();

  } catch (const std::exception& e) {
    LOG_ERR(
      "Exception of type {} caught: {}", boost::core::demangled_name(typeid(e)),
      e.what()
    );
    LOG_ERR("Exiting...");
  }

  // destructors cleans up window and font before quitting sdl and freetype
  // FtDone();
  // SDL_Quit();
}
