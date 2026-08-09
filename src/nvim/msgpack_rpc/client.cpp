#include "./client.hpp"
#include "boost/asio/connect.hpp"
#include "boost/process/v1/io.hpp"
#include "boost/process/v1/start_dir.hpp"
#include "boost/process/start_dir.hpp"
#include "boost/process/v1/search_path.hpp"
#include "msgpack/v3/object_fwd_decl.hpp"
#include "utils/logger.hpp"
#include <unistd.h>

namespace rpc {

Client::~Client() {
  TryDisconnect();

  if (clientType == ClientType::Stdio) {
    readPipe->close();
    writePipe->close();
    if (process.running()) process.terminate();

  } else if (clientType == ClientType::Tcp) {
    if (socket->is_open()) socket->close();
  }

  for (auto& thread : rwThreads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

bool Client::ConnectStdio(
  const std::string& command, bool interactive, const std::string& dir
) {
  clientType = ClientType::Stdio;

  readPipe = std::make_unique<bp::async_pipe>(context);
  writePipe = std::make_unique<bp::async_pipe>(context);

  auto GetEnv = [](const char* name) -> std::string {
    char* value = std::getenv(name);
    return value ? value : "";
  };

  std::string shell = GetEnv("SHELL");
  if (shell.empty()) {
    shell = bp::search_path("zsh").string();
  }

  std::vector<std::string> flags;
  bool isTty = isatty(STDIN_FILENO) || isatty(STDOUT_FILENO);
  if (!isTty) {
    flags.emplace_back("-l");
  }
  if (interactive) {
    flags.emplace_back("-i");
  }
  flags.emplace_back("-c");

  process = bp::child(
    shell, flags, "exec " + command,
    bp::start_dir = dir,
    bp::std_out > *readPipe,
    bp::std_in < *writePipe
  );

  if (!process.running()) {
    LOG_ERR("Failed to start process: {}", command);
    exit = true;
    return false;
  }
  exit = false;

  rwThreads.emplace_back([this]() { DoRead(); });
  rwThreads.emplace_back([this]() { DoWrite(); });

  return true;
}

bool Client::ConnectTcp(std::string_view host, uint16_t port) {
  clientType = ClientType::Tcp;
  socket = std::make_unique<asio::ip::tcp::socket>(context);

  boost::system::error_code ec;
  asio::ip::tcp::resolver resolver(context);
  auto endpoints = resolver.resolve(host, std::to_string(port), ec);
  asio::connect(*socket, endpoints, ec);

  if (ec) {
    socket->close();
    exit = true;
    return false;
  }
  exit = false;

  rwThreads.emplace_back([this]() { DoRead(); });
  rwThreads.emplace_back([this]() { DoWrite(); });

  return true;
}

void Client::TryDisconnect() {
  if (!exit.exchange(true)) {
    msgsOutCv.notify_one();
  }
}

bool Client::IsConnected() {
  return !exit;
}

uint32_t Client::Msgid() {
  return currId++;
}

void Client::DoRead() {
  unpacker.reserve_buffer(readSize);

  while (IsConnected()) {
    auto buffer = asio::buffer(unpacker.buffer(), readSize);

    boost::system::error_code ec;
    std::size_t length;

    if (clientType == ClientType::Stdio) {
      length = readPipe->read_some(buffer, ec);
    } else if (clientType == ClientType::Tcp) {
      length = socket->read_some(buffer, ec);
    }

    if (ec) {
      if (ec == asio::error::eof) {
        LOG_INFO("Client::DoRead: Connection closed");
        TryDisconnect();
        return;
      } 
      LOG_ERR("Client::DoRead: Error - {}", ec.message());
      continue;
    }

    unpacker.buffer_consumed(length);

    msgpack::object_handle handle;
    while (unpacker.next(handle)) {
      const auto& obj = handle.get();

      if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 3) {
        continue;
      }

      try {
        int type = obj.via.array.ptr[0].convert();

        if (type == MessageType::Request) {
          RequestIn request(obj.convert());
          {
            auto msgsAccess = messages.lock();
            auto& message = msgsAccess->emplace(Request{
              .method = request.method,
              .params = request.params,
              ._zone = std::move(handle.zone()),
              .promise{},
            });
            auto& promise = std::get<Request>(message).promise;

            std::thread([weak_self = weak_from_this(), msgid = request.msgid,
                         future = promise.get_future()] mutable {
              try {
                ResponseOut msg{.msgid = msgid};

                // result holds memory zone, it should be in scope when 
                // packing the response
                RequestResult result = future.get();
                if (result) {
                  msg.result = (*result).get();
                } else {
                  msg.error = result.error().get();
                }

                msgpack::sbuffer buffer;
                msgpack::pack(buffer, msg);

                if (auto self = weak_self.lock()) {
                  self->Write(std::move(buffer));
                }

              } catch (...) {
                // Future broken (session destroyed), safe to ignore
                return;
              }
            }).detach();
          }

        } else if (type == MessageType::Response) {
          ResponseIn response(obj.convert());
          {
            std::scoped_lock lock(responsesMutex);

            if (auto it = responses.find(response.msgid); it != responses.end()) {
              auto& promise = it->second;
              if (response.error.is_nil()) {
                promise.set_value(
                  msgpack::object_handle(response.result, std::move(handle.zone()))
                );
              } else {
                // nvim error format: [error_type, error_message]
                std::string errMsg;
                if (response.error.type == msgpack::type::ARRAY &&
                    response.error.via.array.size >= 2 &&
                    response.error.via.array.ptr[1].type == msgpack::type::STR) {
                  auto& str = response.error.via.array.ptr[1].via.str;
                  errMsg = {str.ptr, str.size};

                } else {
                  errMsg = ToString(response.error);
                }
                promise.set_exception(std::make_exception_ptr(std::runtime_error(errMsg)));
              }
              responses.erase(it);

            } else {
              LOG_ERR(
                "Client::DoRead: Response not found for msgid: {}", response.msgid
              );
            }
          }

        } else if (type == MessageType::Notification) {
          NotificationIn notification(obj.convert());
          messages.lock()->emplace(Notification{
            .method = notification.method,
            .params = notification.params,
            ._zone = std::move(handle.zone()),
          });

        } else {
          LOG_WARN("Client::DoRead: Unknown type: {}", type);
        }

      } catch (msgpack::type_error& e) {
        LOG_ERR("Client::DoRead: msgpack::type_error - {}", e.what());
      }
    }

    if (unpacker.buffer_capacity() < readSize) {
      unpacker.reserve_buffer(readSize);
    }
  }
}

void Client::Write(msgpack::sbuffer&& buffer) {
  msgsOut.lock()->push(std::move(buffer));
  msgsOutCv.notify_one();
}

void Client::DoWrite() {
  while (true) {
    {
      auto access = msgsOut.lock();
      msgsOutCv.wait(access.get_lock(), [&] {
        return !access->empty() || exit;
      });
    }
    if (!IsConnected()) break;

    auto& msgBuffer = msgsOut.lock()->front();
    auto buffer = asio::buffer(msgBuffer.data(), msgBuffer.size());

    boost::system::error_code ec;

    if (clientType == ClientType::Stdio) {
      writePipe->write_some(buffer, ec);
    } else if (clientType == ClientType::Tcp) {
      socket->write_some(buffer, ec);
    }

    if (ec) {
      LOG_ERR("Client::DoWrite: {}", ec.message());
    }

    msgsOut.lock()->pop();
  }
}

} // namespace rpc
