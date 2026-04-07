#include "Rudp/BsdClientApp.hpp"

#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "Rudp/BsdUdpSocket.hpp"
#include "Rudp/Config.hpp"
#include "Rudp/Session.hpp"

namespace Rudp::Runtime {
namespace {

using Rudp::Session::ConnectionState;
using Rudp::Session::EndpointKey;
using Rudp::Session::Session;
using Rudp::Session::SessionRole;

[[nodiscard]] std::uint64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

void drain_client_events(Session& session, const LogSink& logger) {
  for (const auto& event : session.drain_events()) {
    auto line = format_session_event("[client]", event);
    line += format_session_stats(session.stats());
    log_line(logger, line);
  }
}

}  // namespace

void run_client_app(std::string_view server_address,
                    std::uint16_t port,
                    const LogSink& logger) {
  auto socket = BsdUdpSocket::create_non_blocking();
  if (!socket.has_value()) {
    return;
  }

  if (!socket->bind(Rudp::Config::current().runtime.client_bind_address, 0)) {
    return;
  }

  const EndpointKey server_endpoint{std::string(server_address), port};
  Session session(SessionRole::Client);
  log_line(logger, std::string("client targeting ") + std::string(server_address) +
                       ':' + std::to_string(port));
  log_line(logger,
           "type a line to send it as unreliable channel 1; /quit exits");

  bool should_exit = false;
  while (!should_exit) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket->native_handle(), &readfds);
    FD_SET(STDIN_FILENO, &readfds);
    const int max_fd = std::max(socket->native_handle(), STDIN_FILENO);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = static_cast<suseconds_t>(
        Rudp::Config::current().runtime.client_select_timeout_us);
    const int ready = ::select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready < 0 && errno != EINTR) {
      std::perror("select");
      break;
    }

    if (FD_ISSET(socket->native_handle(), &readfds)) {
      while (const auto received = socket->recv_from(
                 Rudp::Config::current().runtime.socket_buffer_size)) {
        session.on_datagram_received(received->bytes, now_ms());
      }
    }

    if (FD_ISSET(STDIN_FILENO, &readfds)) {
      std::string line;
      if (!std::getline(std::cin, line)) {
        break;
      }
      if (line == "/quit") {
        break;
      }
      const auto* first = reinterpret_cast<const std::byte*>(line.data());
      session.queue_send(1U, Rudp::ChannelType::Unreliable,
                         std::span<const std::byte>(first, line.size()));
    }

    for (std::uint32_t i = 0;
         i < Rudp::Config::current().runtime.client_poll_budget; ++i) {
      auto outbound = session.poll_tx(now_ms());
      if (!outbound.has_value()) {
        break;
      }
      static_cast<void>(socket->send_to(server_endpoint, *outbound));
    }

    drain_client_events(session, logger);
    if (session.connection_state() == ConnectionState::Reset) {
      log_line(logger, "[client] session entered Reset, exiting");
      should_exit = true;
    }
  }
}

}  // namespace Rudp::Runtime
