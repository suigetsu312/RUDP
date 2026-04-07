#include "Rudp/BsdServerApp.hpp"

#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "Rudp/BsdUdpSocket.hpp"
#include "Rudp/Config.hpp"
#include "Rudp/ServerSessionManager.hpp"

namespace Rudp::Runtime {
namespace {

using Rudp::Session::EndpointKey;
using Rudp::Session::ServerSessionManager;
using Rudp::Session::SessionEvent;

[[nodiscard]] std::uint64_t now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

void drain_server_events(
    ServerSessionManager& manager,
    const LogSink& logger,
    std::optional<std::uint32_t>& preferred_conn_id,
    std::unordered_map<std::uint32_t, EndpointKey>& active_endpoints) {
  for (const auto& wrapped : manager.drain_events()) {
    std::string prefix = "[server] endpoint=" + wrapped.endpoint.address + ':' +
                         std::to_string(wrapped.endpoint.port);
    if (wrapped.conn_id.has_value()) {
      prefix += " conn_id=" + std::to_string(*wrapped.conn_id);
      preferred_conn_id = wrapped.conn_id;
      active_endpoints[*wrapped.conn_id] = wrapped.endpoint;
    }
    auto line = format_session_event(prefix, wrapped.event);
    if (wrapped.conn_id.has_value()) {
      if (const auto stats = manager.active_stats(*wrapped.conn_id);
          stats.has_value()) {
        line += format_session_stats(*stats);
      }
    }
    log_line(logger, line);

    if (wrapped.conn_id.has_value() &&
        (wrapped.event.type == SessionEvent::Type::ConnectionReset ||
         wrapped.event.type == SessionEvent::Type::ConnectionClosed ||
         wrapped.event.type == SessionEvent::Type::Error)) {
      active_endpoints.erase(*wrapped.conn_id);
      if (preferred_conn_id == wrapped.conn_id) {
        preferred_conn_id.reset();
      }
    }
  }
}

[[nodiscard]] std::optional<std::pair<std::uint32_t, std::string>>
parse_server_send_command(
    std::string_view line,
    const std::optional<std::uint32_t>& preferred_conn_id) {
  constexpr std::string_view prefix = "send ";
  if (line.rfind(prefix, 0) == 0) {
    const auto rest = std::string_view(line).substr(prefix.size());
    const auto space = rest.find(' ');
    if (space == std::string_view::npos) {
      return std::nullopt;
    }

    try {
      const auto conn_id = static_cast<std::uint32_t>(
          std::stoul(std::string(rest.substr(0, space))));
      return std::pair{conn_id, std::string(rest.substr(space + 1U))};
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  if (!preferred_conn_id.has_value()) {
    return std::nullopt;
  }
  return std::pair{*preferred_conn_id, std::string(line)};
}

void handle_server_stdin(
    ServerSessionManager& manager,
    const LogSink& logger,
    std::optional<std::uint32_t>& preferred_conn_id,
    const std::unordered_map<std::uint32_t, EndpointKey>& active_endpoints,
    bool& should_exit) {
  std::string line;
  if (!std::getline(std::cin, line)) {
    should_exit = true;
    return;
  }
  if (line == "/quit") {
    should_exit = true;
    return;
  }
  if (line == "/sessions") {
    if (active_endpoints.empty()) {
      log_line(logger, "[server] no active sessions");
    } else {
      for (const auto& [conn_id, endpoint] : active_endpoints) {
        log_line(logger, "[server] active conn_id=" + std::to_string(conn_id) +
                             " endpoint=" + endpoint.address + ':' +
                             std::to_string(endpoint.port));
      }
    }
    return;
  }

  const auto command = parse_server_send_command(line, preferred_conn_id);
  if (!command.has_value()) {
    log_line(logger,
             "[server] unable to resolve target session; use: send <conn_id> <message>");
    return;
  }

  const auto* first =
      reinterpret_cast<const std::byte*>(command->second.data());
  const bool queued =
      manager.queue_send(command->first, 1U, Rudp::ChannelType::Unreliable,
                         std::span<const std::byte>(first, command->second.size()));
  if (!queued) {
    log_line(logger,
             "[server] unknown active conn_id=" + std::to_string(command->first));
  }
}

}  // namespace

void run_server_app(std::string_view bind_address,
                    std::uint16_t port,
                    const LogSink& logger) {
  auto socket = BsdUdpSocket::create_non_blocking();
  if (!socket.has_value()) {
    return;
  }
  if (!socket->bind(bind_address, port)) {
    return;
  }

  ServerSessionManager manager;
  std::optional<std::uint32_t> preferred_conn_id;
  std::unordered_map<std::uint32_t, EndpointKey> active_endpoints;
  log_line(logger, std::string("server listening on ") + std::string(bind_address) +
                       ':' + std::to_string(port));
  log_line(logger,
           "type a line to send to the most recent active client, or use: send <conn_id> <message>");

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
        manager.on_datagram_received(received->endpoint, received->bytes, now_ms());
        drain_server_events(manager, logger, preferred_conn_id, active_endpoints);
      }
    }

    if (FD_ISSET(STDIN_FILENO, &readfds)) {
      handle_server_stdin(manager, logger, preferred_conn_id, active_endpoints,
                          should_exit);
    }

    for (auto& outbound : manager.poll_tx(now_ms())) {
      static_cast<void>(socket->send_to(outbound.endpoint, outbound.bytes));
    }
    drain_server_events(manager, logger, preferred_conn_id, active_endpoints);
    ::usleep(Rudp::Config::current().runtime.server_loop_sleep_us);
  }
}

}  // namespace Rudp::Runtime
