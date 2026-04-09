#include "Rudp/BsdServerApp.hpp"

#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <csignal>
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
#include "Rudp/Utils.hpp"

namespace Rudp::Runtime {
namespace {

using Rudp::Session::EndpointKey;
using Rudp::Session::ServerSessionManager;
using Rudp::Session::SessionEvent;
using Rudp::Config::ChannelDefinition;

std::atomic<bool> g_stop_requested{false};

void handle_stop_signal(int) { g_stop_requested.store(true); }

void install_signal_handlers() {
  std::signal(SIGINT, handle_stop_signal);
  std::signal(SIGTERM, handle_stop_signal);
}

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

[[nodiscard]] const ChannelDefinition* default_channel(
    const Rudp::Config::RuntimeProfile& profile) {
  for (const auto& channel : profile.channels) {
    if (channel.is_default) {
      return &channel;
    }
  }
  return profile.channels.empty() ? nullptr : &profile.channels.front();
}

[[nodiscard]] const ChannelDefinition* find_channel(
    const Rudp::Config::RuntimeProfile& profile,
    std::string_view token) {
  for (const auto& channel : profile.channels) {
    if (channel.name == token) {
      return &channel;
    }
  }

  try {
    const auto id = static_cast<std::uint32_t>(std::stoul(std::string(token)));
    for (const auto& channel : profile.channels) {
      if (channel.id == id) {
        return &channel;
      }
    }
  } catch (const std::exception&) {
  }

  return nullptr;
}

void log_channels(const Rudp::Config::RuntimeProfile& profile,
                  const LogSink& logger) {
  for (const auto& channel : profile.channels) {
    std::string line = "[server] channel id=" + std::to_string(channel.id);
    line += " name=" + channel.name;
    line += " type=" + Rudp::Utils::channelTypeName(channel.type);
    if (channel.is_default) {
      line += " default=true";
    }
    log_line(logger, line);
  }
}

void log_server_summaries(
    ServerSessionManager& manager,
    const LogSink& logger,
    const std::unordered_map<std::uint32_t, EndpointKey>& active_endpoints) {
  if (active_endpoints.empty()) {
    log_line(logger, "[server] summary no-active-sessions");
    return;
  }

  for (const auto& [conn_id, endpoint] : active_endpoints) {
    const auto stats = manager.active_stats(conn_id);
    if (!stats.has_value()) {
      continue;
    }

    std::string prefix = "[server] endpoint=" + endpoint.address + ':' +
                         std::to_string(endpoint.port) + " conn_id=" +
                         std::to_string(conn_id);
    log_line(logger, format_session_summary(prefix, *stats));
  }
}

struct ServerSendCommand final {
  std::uint32_t conn_id = 0;
  const ChannelDefinition* channel = nullptr;
  std::string payload;
};

[[nodiscard]] std::optional<ServerSendCommand> parse_server_send_command(
    const Rudp::Config::RuntimeProfile& profile,
    std::string_view line,
    const std::optional<std::uint32_t>& preferred_conn_id) {
  constexpr std::string_view prefix = "send ";
  if (line.rfind(prefix, 0) == 0) {
    const auto rest = std::string_view(line).substr(prefix.size());
    const auto first_space = rest.find(' ');
    if (first_space == std::string_view::npos) {
      return std::nullopt;
    }
    const auto second_space = rest.find(' ', first_space + 1U);
    if (second_space == std::string_view::npos) {
      return std::nullopt;
    }

    try {
      const auto conn_id = static_cast<std::uint32_t>(
          std::stoul(std::string(rest.substr(0, first_space))));
      const auto* channel = find_channel(
          profile, rest.substr(first_space + 1U, second_space - first_space - 1U));
      if (channel == nullptr) {
        return std::nullopt;
      }
      return ServerSendCommand{
          .conn_id = conn_id,
          .channel = channel,
          .payload = std::string(rest.substr(second_space + 1U)),
      };
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  if (!preferred_conn_id.has_value()) {
    return std::nullopt;
  }
  const auto* channel = default_channel(profile);
  if (channel == nullptr) {
    return std::nullopt;
  }
  return ServerSendCommand{
      .conn_id = *preferred_conn_id,
      .channel = channel,
      .payload = std::string(line),
  };
}

void handle_server_stdin(
    const Rudp::Config::RuntimeProfile& profile,
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
  if (line == "/channels") {
    log_channels(profile, logger);
    return;
  }

  const auto command =
      parse_server_send_command(profile, line, preferred_conn_id);
  if (!command.has_value()) {
    log_line(logger,
             "[server] unable to resolve target session; use: send <conn_id> <channel> <message>");
    return;
  }

  const auto* first =
      reinterpret_cast<const std::byte*>(command->payload.data());
  const bool queued = manager.queue_send(
      command->conn_id, command->channel->id, command->channel->type,
      std::span<const std::byte>(first, command->payload.size()));
  if (!queued) {
    log_line(logger,
             "[server] unknown active conn_id=" + std::to_string(command->conn_id));
  }
}

}  // namespace

void run_server_app(const Rudp::Config::RuntimeProfile& profile,
                    const LogSink& logger) {
  install_signal_handlers();
  g_stop_requested.store(false);
  auto socket = BsdUdpSocket::create_non_blocking();
  if (!socket.has_value()) {
    return;
  }
  if (!socket->bind(profile.bind_address, profile.bind_port)) {
    return;
  }

  ServerSessionManager manager;
  std::optional<std::uint32_t> preferred_conn_id;
  std::unordered_map<std::uint32_t, EndpointKey> active_endpoints;
  bool stdin_enabled = ::isatty(STDIN_FILENO) != 0;
  log_line(logger, std::string("server listening on ") + profile.bind_address +
                       ':' + std::to_string(profile.bind_port));
  if (stdin_enabled) {
    log_line(logger,
             "type a line to send on the default channel, or use: send <conn_id> <channel> <message>, /channels");
  } else {
    log_line(logger,
             "[server] stdin is not interactive; runtime commands disabled");
  }

  bool should_exit = false;
  while (!should_exit) {
    if (g_stop_requested.load()) {
      should_exit = true;
      break;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket->native_handle(), &readfds);
    int max_fd = socket->native_handle();
    if (stdin_enabled) {
      FD_SET(STDIN_FILENO, &readfds);
      max_fd = std::max(max_fd, STDIN_FILENO);
    }

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = static_cast<suseconds_t>(
        profile.select_timeout_us);
    const int ready = ::select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
    if (ready < 0 && errno != EINTR) {
      std::perror("select");
      break;
    }

    if (FD_ISSET(socket->native_handle(), &readfds)) {
      while (const auto received = socket->recv_from(
                 profile.socket_buffer_size)) {
        manager.on_datagram_received(received->endpoint, received->bytes, now_ms());
        drain_server_events(manager, logger, preferred_conn_id, active_endpoints);
      }
    }

    if (stdin_enabled && FD_ISSET(STDIN_FILENO, &readfds)) {
      if (!std::cin.good()) {
        stdin_enabled = false;
      } else {
        handle_server_stdin(profile, manager, logger, preferred_conn_id,
                            active_endpoints, should_exit);
        if (!std::cin.good()) {
          stdin_enabled = false;
        }
      }
    }

    for (auto& outbound : manager.poll_tx(now_ms())) {
      static_cast<void>(socket->send_to(outbound.endpoint, outbound.bytes));
    }
    drain_server_events(manager, logger, preferred_conn_id, active_endpoints);
    ::usleep(profile.loop_sleep_us);
  }

  log_server_summaries(manager, logger, active_endpoints);
}

}  // namespace Rudp::Runtime
