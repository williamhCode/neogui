#pragma once

#include "app/options.hpp"
#include "event/neogurt_cmd.hpp"
#include "session/options.hpp"
#include "session/state.hpp"
#include "app/sdl_window.hpp"
#include "gfx/renderer.hpp"
#include <deque>
#include <memory>
#include <string>
#include <string_view>

enum class SpawnMode {
  Child,
  Detached,
};

struct SessionNewOpts {
  std::string name;
  std::string dir;
  bool switchTo = true;
};

struct SessionListOpts {
  // id, name, time (recency)
  std::string sort = "id";
  bool reverse = false;
};

struct SessionListEntry {
  int id;
  std::string name;
  std::string dir;
  MSGPACK_DEFINE_MAP(id, name, dir);
};

using SessionHandle = std::shared_ptr<Session>;

struct SessionManager {
private:
  SpawnMode mode;

  const StartupOptions& startupOpts;
  GlobalOptions& globalOpts;
  sdl::Window& window;
  SizeHandler& sizes;
  Renderer& renderer;

  int currId = 1;
  SessionHandle nullSession{nullptr};
  // sessions are only owned by sessions map and main thread
  // others should hold pointer/reference to sessions in the sessions map
  std::map<int, SessionHandle> sessions;

  // front to back = recency
  std::deque<SessionHandle*> sessionsOrder;

public:
  SessionManager(
    SpawnMode mode,
    const StartupOptions& startupOpts,
    GlobalOptions& globalOpts,
    sdl::Window& window,
    SizeHandler& sizes,
    Renderer& renderer
  );

  auto& Sessions() { return sessions; }
  SessionHandle& CurrSession() {
    auto it = sessionsOrder.begin();
    return it == sessionsOrder.end() ? nullSession : **it;
  }

  bool updateVsync = false;

  void OptionSet(SessionHandle& session, const event::OptionTable& optionTable);

  int SessionNew(const SessionNewOpts& opts = {});  // returns session id (raise error if failed)
  bool SessionEdit(int id, std::string_view name);  // returns success
  bool SessionKill(int id);                         // returns success
  int SessionRestart(int id, const std::string& cmd, bool currDir = false); // returns session id (-1 if failed)
  bool SessionSwitch(int id);                       // returns success
  bool SessionPrev();                               // returns success
  SessionListEntry SessionInfo(int id);             // returns {} if failed
  std::vector<SessionListEntry> SessionList(const SessionListOpts& opts = {});
  void SessionVimCmd(int id, const std::string& cmd, rpc::Request& request); // sets cmd output (nil if target disconnected while executing)
  SessionHandle* GetCurrentSession(); // returns nullptr if should quit

  void FontSizeChange(float delta, bool all = false);
  void FontSizeReset(bool all = false);

  void UpdateUiOptions(SessionHandle& session);

  void UpdateSessionSizes(SessionHandle& session);

private:
  // all session switching leads to this function
  void SessionSwitchInternal(SessionHandle& session);

  // void LoadSessions(std::string_view filename);
  // void SaveSessions(std::string_view filename);

  // void SpawnNvimProcess(uint16_t port);
};
