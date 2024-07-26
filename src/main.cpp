#include "SDL3/SDL_init.h"
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

    SessionManager sessionManager(SpawnMode::Child, options, window, sizes, renderer);
    auto errMsg = sessionManager.NewSession();
    if (!errMsg.empty()) {
      LOG_ERR("Failed to create default session: {}", errMsg);
      return 1;
    }
    SessionState* session = sessionManager.Curr();

    // main loop -----------------------------------
    // lock whenever ctx.device is used
    std::mutex wgpuDeviceMutex;

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

        // session->nvim events -------------------------------------------
        if (sessionManager.Update()) {
          SDL_Event quitEvent{.type = SDL_EVENT_QUIT};
          SDL_PushEvent(&quitEvent);
          break;
        };
        session = sessionManager.Curr();

        ProcessUserEvents(*session->nvim.client, sessionManager);
        session = sessionManager.Curr();

        LOG_DISABLE();
        ParseUiEvents(*session->nvim.client, session->nvim.uiEvents);
        LOG_ENABLE();

        // process events ---------------------------------------
        {
          std::scoped_lock lock(wgpuDeviceMutex);
          LOG_DISABLE();
          if (ParseEditorState(session->nvim.uiEvents, session->editorState)) {
            idle = false;
            idleElasped = 0;
          }
          LOG_ENABLE();
        }

        // update ----------------------------------------------
        while (!resizeEvents.Empty()) {
          // only process the last 2 resize events
          if (resizeEvents.Size() <= 2) {
            auto& event = resizeEvents.Front();
            switch (event.type) {
              case SDL_EVENT_WINDOW_RESIZED:
                window.size = {event.window.data1, event.window.data2};
                break;
              case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                // LOG(
                //   "pixel size changed: {} {}", event.window.data1, event.window.data2
                // );
                window.fbSize = {event.window.data1, event.window.data2};

                std::scoped_lock lock(wgpuDeviceMutex);
                auto uiFbSize = sizes.uiFbSize;
                sizes.UpdateSizes(
                  window.size, window.dpiScale,
                  session->editorState.fontFamily.DefaultFont().charSize,
                  options.margins
                );
                sdl::Window::_ctx.Resize(sizes.fbSize);

                if (uiFbSize == sizes.uiFbSize) {
                  renderer.camera.Resize(sizes.size);
                  renderer.finalRenderTexture.UpdatePos(sizes.offset);

                } else {
                  renderer.Resize(sizes);
                  session->nvim.UiTryResize(sizes.uiWidth, sizes.uiHeight);
                }
                break;
              }
            }
          }
          resizeEvents.Pop();
        }

        while (!sdlEvents.Empty()) {
          auto& event = sdlEvents.Front();
          switch (event.type) {
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
              windowFocused = true;
              idle = false;
              idleElasped = 0;
              session->editorState.cursor.blinkState = BlinkState::Wait;
              session->editorState.cursor.blinkElasped = 0;
              break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
              windowFocused = false;
              break;
              // case SDL_EVENT_KEY_DOWN:
              //   if (event.key.keysym.sym == SDLK_1) {
              //     LOG("key down: {}", event.key.keysym.sym);
              //     session->nvim.NvimListUis();
              //   }
              //   if (event.key.keysym.sym == SDLK_2) {
              //     LOG("key down: {}", event.key.keysym.sym);
              //     session->nvim.UiDetach();
              //   }
              //   break;
          }
          sdlEvents.Pop();
        }

        session->editorState.winManager.UpdateScrolling(dt);

        auto* activeWin =
          session->editorState.winManager.GetWin(session->editorState.cursor.grid);
        if (activeWin) {
          auto cursorPos =
            glm::vec2{
              activeWin->startCol + session->editorState.cursor.col,
              activeWin->startRow + session->editorState.cursor.row,
            } *
              sizes.charSize +
            sizes.offset;

          auto& winTex = activeWin->sRenderTexture;
          auto scrollOffset = //
            winTex.scrolling  //
              ? glm::vec2(0, (winTex.scrollDist - winTex.scrollCurr))
              : glm::vec2(0);

          session->editorState.cursor.SetDestPos(cursorPos + scrollOffset);
        }
        session->editorState.cursor.Update(dt);

        // render ----------------------------------------------
        if (auto hlIter = session->editorState.hlTable.find(0);
            hlIter != session->editorState.hlTable.end()) {
          auto color = hlIter->second.background.value();
          renderer.SetClearColor(color);
        }

        if (idle) continue;
        idleElasped += dt;
        if (idleElasped >= options.cursorIdleTime) {
          idle = true;
          session->editorState.cursor.blinkState = BlinkState::On;
        }

        if (!windowFocused) {
          session->editorState.cursor.blinkState = BlinkState::On;
        }

        {
          std::scoped_lock lock(wgpuDeviceMutex);
          renderer.Begin();

          bool renderWindows = false;
          for (auto& [id, win] : session->editorState.winManager.windows) {
            if (win.grid.dirty) {
              renderer.RenderToWindow(
                win, session->editorState.fontFamily, session->editorState.hlTable
              );
              win.grid.dirty = false;
              renderWindows = true;
            }
          }

          if (session->editorState.cursor.dirty && activeWin != nullptr) {
            renderer.RenderCursorMask(
              *activeWin, session->editorState.cursor, session->editorState.fontFamily,
              session->editorState.hlTable
            );
            session->editorState.cursor.dirty = false;
          }

          if (renderWindows || session->editorState.winManager.dirty) {
            session->editorState.winManager.dirty = false;

            std::vector<const Win*> windows;
            if (auto* msgWin = session->editorState.winManager.GetMsgWin()) {
              windows.push_back(msgWin);
            }
            std::vector<const Win*> floatWindows;
            for (auto& [id, win] : session->editorState.winManager.windows) {
              if (id == 1 || id == session->editorState.winManager.msgWinId ||
                  win.hidden) {
                continue;
              }
              if (win.IsFloating()) {
                floatWindows.push_back(&win);
              } else {
                windows.push_back(&win);
              }
            }
            if (auto winIt = session->editorState.winManager.windows.find(1);
                winIt != session->editorState.winManager.windows.end()) {
              windows.push_back(&winIt->second);
            }

            // sort floating windows by zindex
            std::ranges::sort(floatWindows, [](const Win* win, const Win* other) {
              return win->floatData->zindex < other->floatData->zindex;
            });

            renderer.RenderWindows(windows, floatWindows);
            // switch to current texture only after rendering to it
            if (renderer.prevFinalRenderTexture.texture) {
              renderer.prevFinalRenderTexture = {};
            }
          }

          renderer.RenderFinalTexture();

          if (session->editorState.cursor.ShouldRender()) {
            renderer.RenderCursor(
              session->editorState.cursor, session->editorState.hlTable
            );
          }

          renderer.End();

          ctx.surface.Present();
          ctx.device.Tick();
        }

        // timer.End();
        // auto avgDuration = duration_cast<microseconds>(timer.GetAverageDuration());
        // std::cout << '\r' << avgDuration << std::string(10, ' ') << std::flush;
      }
    });

    // event loop --------------------------------
    SDL_StartTextInput(window.Get());
    SDL_Rect rect{0, 0, 100, 100};
    SDL_SetTextInputArea(window.Get(), &rect, 0);

    // resize handling
    sdl::AddEventWatch([&](SDL_Event& event) {
      switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
          resizeEvents.Push(event);
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          resizeEvents.Push(event);
          break;
        }
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
          session->inputHandler.HandleKeyboard(event.key);
          sdlEvents.Push(event);
          break;

        case SDL_EVENT_TEXT_EDITING:
          break;
        case SDL_EVENT_TEXT_INPUT:
          session->inputHandler.HandleTextInput(event.text);
          break;

        // mouse handling ------------------------
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
          session->inputHandler.HandleMouseButton(event.button);
          break;
        case SDL_EVENT_MOUSE_MOTION:
          session->inputHandler.HandleMouseMotion(event.motion);
          break;
        case SDL_EVENT_MOUSE_WHEEL:
          session->inputHandler.HandleMouseWheel(event.wheel);
          break;

        // window handling -----------------------
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: {
          std::scoped_lock lock(wgpuDeviceMutex);
          float prevDpiScale = window.dpiScale;
          window.dpiScale = SDL_GetWindowPixelDensity(window.Get());
          if (prevDpiScale == window.dpiScale) break;
          // LOG("display scale changed: {}", window.dpiScale);
          session->editorState.fontFamily.ChangeDpiScale(window.dpiScale);
          session->editorState.cursor.Init(sizes.charSize, sizes.dpiScale);
          break;
        }

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
          sdlEvents.Push(event);
          break;
      }
    }

    renderThread.join();
    // if (session->nvim.IsConnected()) {
      // send escape so session->nvim doesn't get stuck when reattaching
      // prevents cmd + q exiting window getting stuck
      // session->nvim.Input("<Esc>");
      // session->nvim.UiDetach();
      // LOG_INFO("Detached UI");
    // }

  } catch (const std::exception& e) {
    LOG_ERR(
      "Exception of type {} caught: {}", boost::core::demangled_name(typeid(e)),
      e.what()
    );
    LOG_ERR("Exiting...");
  }
  // destructors cleans up window and font before quitting sdl and freetype

  FtDone();
  SDL_Quit();
}
