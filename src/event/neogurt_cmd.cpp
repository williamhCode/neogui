#include "./neogurt_cmd.hpp"
#include "session/manager.hpp"
#include <string_view>

void ProcessNeogurtCmd(
  rpc::Request& request, SessionHandle& session, SessionManager& sessionManager
) {
  using msgpack::type::nil_t;

  try {
    auto [cmd, opts] = request.params.as<event::NeogurtCmd>();

    auto conv = [&](std::string_view key) {
      return opts.at(key).convert();
    };
    auto convId = [&] {
      int id = conv("id");
      return id == 0 ? session->id : id;
    };

    if (cmd == "option_set") {
      sessionManager.OptionSet(session, opts);
      request.SetResult(nil_t());

    } else if (cmd == "session_new") {
      int id = sessionManager.SessionNew({
        .name = conv("name"),
        .dir = conv("dir"),
        .switchTo = conv("switch_to"),
      });
      request.SetResult(id);

    } else if (cmd == "session_edit") {
      bool success = sessionManager.SessionEdit(convId(), conv("name"));
      request.SetResult(success);

    } else if (cmd == "session_kill") {
      bool success = sessionManager.SessionKill(convId());
      request.SetResult(success);

    } else if (cmd == "session_restart") {
      int id = sessionManager.SessionRestart(convId(), conv("cmd"), conv("curr_dir"));
      if (id == -1) {
        request.SetResult(nil_t());
      } else {
        request.SetResult(id);
      }

    } else if (cmd == "session_switch") {
      bool success = sessionManager.SessionSwitch(convId());
      request.SetResult(success);

    } else if (cmd == "session_prev") {
      bool success = sessionManager.SessionPrev();
      request.SetResult(success);

    } else if (cmd == "session_info") {
      auto info = sessionManager.SessionInfo(convId());
      if (info.id == 0) {
        request.SetResult(nil_t());
      } else {
        request.SetResult(info);
      }

    } else if (cmd == "session_list") {
      std::vector<SessionListEntry> list = sessionManager.SessionList({
        .sort = conv("sort"),
        .reverse = conv("reverse"),
      });
      request.SetResult(list);

    } else if (cmd == "session_vim_cmd") {
      sessionManager.SessionVimCmd(convId(), conv("cmd"), request);

    } else if (cmd == "font_size_change") {
      sessionManager.FontSizeChange(conv("arg1"), conv("all"));
      request.SetResult(nil_t());

    } else if (cmd == "font_size_reset") {
      sessionManager.FontSizeReset(conv("all"));
      request.SetResult(nil_t());

    } else {
      request.SetError("Unknown command: " + std::string(cmd));
    }

  } catch (const std::exception& e) {
    request.SetError(e.what());
  }
}
