#include "manager.hpp"
#include "session/state.hpp"
#include "app/options.hpp"
#include "app/sdl_window.hpp"
#include "gfx/renderer.hpp"
#include "utils/color.hpp"

SessionManager::~SessionManager() {
}

SessionManager::SessionManager(
  SpawnMode _mode,
  Options& _options,
  sdl::Window& _window,
  SizeHandler& _sizes,
  Renderer& _renderer
)
  : mode(_mode), options(_options), window(_window), sizes(_sizes),
    renderer(_renderer) {
}

std::string SessionManager::NewSession(const NewSessionOpts& opts) {
  bool first = sessions.empty();
  int id = currId++;
  auto [it, success] = sessions.try_emplace(id, SessionState{
    .id = id,
    .name = opts.name,
    .nvim{},
    .editorState{},
    .inputHandler{},
  });
  if (!success) {
    return "Session with id " + std::to_string(id) + " already exists";
  }

  auto& session = it->second;
  if (!session.nvim.ConnectStdio(opts.dir)) {
    return "Failed to connect to nvim";
  }

  options.Load(session.nvim);

  if (first) {
    window = sdl::Window({1200, 800}, "Neovim GUI", options.window);
  }

  std::string guifont = session.nvim.GetOptionValue("guifont", {}).get()->convert();
  auto fontFamilyResult = FontFamily::FromGuifont(guifont, window.dpiScale);
  if (!fontFamilyResult) {
    return "Failed to create font family: " + fontFamilyResult.error();
  }
  session.editorState.fontFamily = std::move(*fontFamilyResult);

  sizes.UpdateSizes(
    window.size, window.dpiScale,
    session.editorState.fontFamily.DefaultFont().charSize, options.margins
  );

  if (first) {
    renderer = Renderer(sizes);
  }

  session.editorState.Init(sizes);

  if (options.transparency < 1) {
    auto& hl = session.editorState.hlTable[0];
    hl.background = IntToColor(options.bgColor);
    hl.background->a = options.transparency;
  }

  session.inputHandler = InputHandler(
    &session.nvim, &session.editorState.winManager, options.macOptIsMeta,
    options.multigrid, options.scrollSpeed
  );

  if (opts.switchTo) {
    session.nvim.UiAttach(
      sizes.uiWidth, sizes.uiHeight,
      {
        {"rgb", true},
        {"ext_multigrid", options.multigrid},
        {"ext_linegrid", true},
      }
    ).wait();

    if (currSession != nullptr) {
      // currSession->nvim.UiDetach().wait();
      // currSession->editorState = {};
      // currSession->inputHandler = {};
    }
    prevSession = currSession;
    currSession = &session;
  }

  return "";
}

std::string SessionManager::SwitchSession(int id) {
  auto it = sessions.find(id);
  if (it == sessions.end()) {
    return "Session with id " + std::to_string(id) + " not found";
  }

  auto& session = it->second;
  session.nvim.UiAttach(
    sizes.uiWidth, sizes.uiHeight,
    {
      {"rgb", true},
      {"ext_multigrid", options.multigrid},
      {"ext_linegrid", true},
    }
  ).wait();

  currSession->nvim.UiDetach().wait();

  prevSession = currSession;
  currSession = &session;

  return "";
}

bool SessionManager::Update() {
  if (!currSession->nvim.IsConnected()) {
    sessions.erase(currSession->id);
    if (prevSession == nullptr) return true;

    currSession = prevSession;
    prevSession = nullptr;
    auto& session = *currSession;

    options.Load(session.nvim);

    sizes.UpdateSizes(
      window.size, window.dpiScale,
      session.editorState.fontFamily.DefaultFont().charSize, options.margins
    );

    renderer.Resize(sizes);

    if (options.transparency < 1) {
      auto& hl = session.editorState.hlTable[0];
      hl.background = IntToColor(options.bgColor);
      hl.background->a = options.transparency;
    }

    // session.nvim.UiAttach(
    //   sizes.uiWidth, sizes.uiHeight,
    //   {
    //     {"rgb", true},
    //     {"ext_multigrid", options.multigrid},
    //     {"ext_linegrid", true},
    //   }
    // ).wait();
  }

  return sessions.empty();
}

// void SessionManager::LoadSessions(std::string_view filename) {
//   std::ifstream file(filename);
//   std::string line;
//   while (std::getline(file, line)) {
//     std::istringstream iss(line);
//     std::string session_name;
//     uint16_t port;
//     if (iss >> session_name >> port) {
//       sessions[session_name] = port;
//     }
//   }
// }

// void SessionManager::SaveSessions(std::string_view filename) {
//   std::ofstream file(filename);
//   for (auto& [key, value] : sessions) {
//     file << key << " " << value << "\n";
//   }
// }

// static uint16_t FindFreePort() {
//   using namespace boost::asio;
//   io_service io_service;
//   ip::tcp::acceptor acceptor(io_service);
//   ip::tcp::endpoint endpoint(ip::tcp::v4(), 0);
//   boost::system::error_code ec;
//   acceptor.open(endpoint.protocol(), ec);
//   acceptor.bind(endpoint, ec);
//   if (ec) {
//     throw std::runtime_error("Failed to bind to open port: " + ec.message());
//   }
//   return acceptor.local_endpoint().port();
// }

// void SessionManager::SpawnNvimProcess(uint16_t port) {
//   std::string luaInitPath = ROOT_DIR "/lua/init.lua";
//   std::string cmd = "nvim --listen localhost:" + std::to_string(port) +
//                     " --headless ";
//   LOG_INFO("Spawning nvim process: {}", cmd);
//   bp::child child(cmd);
//   child.detach();
// }

