#include "Rudp/BsdClientApp.hpp"

#include <sys/select.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Rudp/BsdUdpSocket.hpp"
#include "Rudp/Config.hpp"
#include "Rudp/Session.hpp"

namespace Rudp::Runtime {
namespace {

using Rudp::Session::ConnectionState;
using Rudp::Session::EndpointKey;
using Rudp::Session::Session;
using Rudp::Session::SessionRole;
using Rudp::Config::ChannelDefinition;

[[nodiscard]] std::string to_string(Rudp::ChannelType type) {
  switch (type) {
    case Rudp::ChannelType::ReliableOrdered:
      return "reliable_ordered";
    case Rudp::ChannelType::ReliableUnordered:
      return "reliable_unordered";
    case Rudp::ChannelType::Unreliable:
      return "unreliable";
    case Rudp::ChannelType::MonotonicState:
      return "monotonic_state";
  }
  return "unknown";
}

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
    std::string line = "[client] channel id=" + std::to_string(channel.id);
    line += " name=" + channel.name;
    line += " type=" + to_string(channel.type);
    if (channel.is_default) {
      line += " default=true";
    }
    log_line(logger, line);
  }
}

[[nodiscard]] std::optional<std::pair<const ChannelDefinition*, std::string>>
parse_client_send_command(const Rudp::Config::RuntimeProfile& profile,
                          std::string_view line) {
  constexpr std::string_view prefix = "send ";
  if (line.rfind(prefix, 0) == 0) {
    const auto rest = line.substr(prefix.size());
    const auto space = rest.find(' ');
    if (space == std::string_view::npos) {
      return std::nullopt;
    }

    const auto* channel = find_channel(profile, rest.substr(0, space));
    if (channel == nullptr) {
      return std::nullopt;
    }
    return std::pair{channel, std::string(rest.substr(space + 1U))};
  }

  const auto* channel = default_channel(profile);
  if (channel == nullptr) {
    return std::nullopt;
  }
  return std::pair{channel, std::string(line)};
}

struct QueuedSend final {
  std::uint32_t channel_id = 0;
  Rudp::ChannelType channel_type = Rudp::ChannelType::Unreliable;
  std::string payload;
};

struct SpawnCommand final {
  const ChannelDefinition* channel = nullptr;
  std::uint32_t thread_count = 0;
  std::uint32_t message_count = 0;
  std::uint32_t interval_ms = 0;
  std::string payload;
};

class LoadGenerator final {
 public:
  LoadGenerator() = default;

  ~LoadGenerator() { stop_all(); }

  LoadGenerator(const LoadGenerator&) = delete;
  LoadGenerator& operator=(const LoadGenerator&) = delete;

  void spawn(const ChannelDefinition& channel,
             std::uint32_t thread_count,
             std::uint32_t message_count,
             std::uint32_t interval_ms,
             std::string payload) {
    stop_requested_.store(false);
    for (std::uint32_t i = 0; i < thread_count; ++i) {
      workers_.emplace_back([this, channel_id = channel.id,
                             channel_type = channel.type, message_count,
                             interval_ms, payload, worker_index = i]() {
        std::uint32_t produced = 0;
        while (!stop_requested_.load()) {
          if (message_count != 0 && produced >= message_count) {
            break;
          }

          {
            std::scoped_lock lock(queue_mutex_);
            queue_.push_back(QueuedSend{
                .channel_id = channel_id,
                .channel_type = channel_type,
                .payload = payload + " [t" + std::to_string(worker_index) +
                           " #" + std::to_string(produced) + ']',
            });
          }

          ++produced;
          if (interval_ms == 0) {
            std::this_thread::yield();
          } else {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(interval_ms));
          }
        }
      });
    }
  }

  void stop_all() {
    stop_requested_.store(true);
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    workers_.clear();
  }

  [[nodiscard]] std::vector<QueuedSend> drain() {
    std::scoped_lock lock(queue_mutex_);
    std::vector<QueuedSend> drained;
    drained.reserve(queue_.size());
    while (!queue_.empty()) {
      drained.push_back(std::move(queue_.front()));
      queue_.pop_front();
    }
    return drained;
  }

  [[nodiscard]] std::size_t worker_count() const noexcept {
    return workers_.size();
  }

 private:
  std::atomic<bool> stop_requested_{false};
  mutable std::mutex queue_mutex_;
  std::deque<QueuedSend> queue_;
  std::vector<std::thread> workers_;
};

[[nodiscard]] std::optional<SpawnCommand> parse_spawn_command(
    const Rudp::Config::RuntimeProfile& profile,
    std::string_view line) {
  constexpr std::string_view prefix = "/spawn ";
  if (line.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }

  auto rest = line.substr(prefix.size());
  const auto first_space = rest.find(' ');
  if (first_space == std::string_view::npos) {
    return std::nullopt;
  }
  const auto second_space = rest.find(' ', first_space + 1U);
  if (second_space == std::string_view::npos) {
    return std::nullopt;
  }
  const auto third_space = rest.find(' ', second_space + 1U);
  if (third_space == std::string_view::npos) {
    return std::nullopt;
  }
  const auto fourth_space = rest.find(' ', third_space + 1U);
  if (fourth_space == std::string_view::npos) {
    return std::nullopt;
  }

  const auto* channel = find_channel(profile, rest.substr(0, first_space));
  if (channel == nullptr) {
    return std::nullopt;
  }

  try {
    const auto thread_count = static_cast<std::uint32_t>(std::stoul(
        std::string(rest.substr(first_space + 1U,
                                second_space - first_space - 1U))));
    const auto message_count = static_cast<std::uint32_t>(std::stoul(
        std::string(rest.substr(second_space + 1U,
                                third_space - second_space - 1U))));
    const auto interval_ms = static_cast<std::uint32_t>(std::stoul(
        std::string(rest.substr(third_space + 1U,
                                fourth_space - third_space - 1U))));
    return SpawnCommand{
        .channel = channel,
        .thread_count = thread_count,
        .message_count = message_count,
        .interval_ms = interval_ms,
        .payload = std::string(rest.substr(fourth_space + 1U)),
    };
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool process_client_command(const Rudp::Config::RuntimeProfile& profile,
                            Session& session,
                            LoadGenerator& load_generator,
                            const LogSink& logger,
                            std::string_view line,
                            bool& should_exit) {
  if (line == "/quit") {
    should_exit = true;
    return true;
  }
  if (line == "/channels") {
    log_channels(profile, logger);
    return true;
  }
  if (line == "/workers") {
    log_line(logger,
             "[client] load workers=" +
                 std::to_string(load_generator.worker_count()));
    return true;
  }
  if (line == "/stop-load") {
    load_generator.stop_all();
    log_line(logger, "[client] load workers stopped");
    return true;
  }
  if (const auto spawn = parse_spawn_command(profile, line); spawn.has_value()) {
    load_generator.spawn(*spawn->channel, spawn->thread_count,
                         spawn->message_count, spawn->interval_ms,
                         std::move(spawn->payload));
    log_line(logger,
             "[client] spawned " + std::to_string(spawn->thread_count) +
                 " workers on " + spawn->channel->name + " count=" +
                 std::to_string(spawn->message_count) + " interval_ms=" +
                 std::to_string(spawn->interval_ms));
    return true;
  }

  const auto command = parse_client_send_command(profile, line);
  if (!command.has_value()) {
    log_line(logger,
             "[client] unknown command; use: send <channel> <message> or /spawn <channel> <threads> <count> <interval-ms> <payload>");
    return false;
  }

  const auto* first =
      reinterpret_cast<const std::byte*>(command->second.data());
  session.queue_send(command->first->id, command->first->type,
                     std::span<const std::byte>(first, command->second.size()));
  return true;
}

void run_bootstrap_commands(const Rudp::Config::RuntimeProfile& profile,
                            Session& session,
                            LoadGenerator& load_generator,
                            const LogSink& logger,
                            bool& should_exit) {
  const char* path = std::getenv("RUDP_CLIENT_BOOTSTRAP_FILE");
  if (path == nullptr || *path == '\0') {
    return;
  }

  std::ifstream input(path);
  if (!input.is_open()) {
    log_line(logger, std::string("[client] failed to open bootstrap file: ") +
                         path);
    return;
  }

  std::string line;
  while (!should_exit && std::getline(input, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    log_line(logger, "[client] bootstrap> " + line);
    static_cast<void>(process_client_command(profile, session, load_generator,
                                             logger, line, should_exit));
  }
}

}  // namespace

void run_client_app(const Rudp::Config::RuntimeProfile& profile,
                    const LogSink& logger) {
  auto socket = BsdUdpSocket::create_non_blocking();
  if (!socket.has_value()) {
    return;
  }

  if (!socket->bind(profile.bind_address, profile.bind_port)) {
    return;
  }

  const EndpointKey server_endpoint{profile.remote_address, profile.remote_port};
  Session session(SessionRole::Client);
  LoadGenerator load_generator;
  log_line(logger, std::string("client targeting ") + profile.remote_address +
                       ':' + std::to_string(profile.remote_port));
  log_line(logger,
           "type a line to send it on the default channel; use: send <channel> <message>, /spawn <channel> <threads> <count> <interval-ms> <payload>, /workers, /stop-load, /channels, /quit");

  bool should_exit = false;
  run_bootstrap_commands(profile, session, load_generator, logger, should_exit);
  while (!should_exit) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket->native_handle(), &readfds);
    FD_SET(STDIN_FILENO, &readfds);
    const int max_fd = std::max(socket->native_handle(), STDIN_FILENO);

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
        session.on_datagram_received(received->bytes, now_ms());
      }
    }

    if (FD_ISSET(STDIN_FILENO, &readfds)) {
      std::string line;
      if (!std::getline(std::cin, line)) {
        break;
      }
      static_cast<void>(process_client_command(profile, session, load_generator,
                                               logger, line, should_exit));
      if (should_exit) {
        break;
      }
    }

    for (auto& generated : load_generator.drain()) {
      const auto* first =
          reinterpret_cast<const std::byte*>(generated.payload.data());
      session.queue_send(generated.channel_id, generated.channel_type,
                         std::span<const std::byte>(first,
                                                    generated.payload.size()));
    }

    for (std::uint32_t i = 0;
         i < profile.poll_budget; ++i) {
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

  load_generator.stop_all();
}

}  // namespace Rudp::Runtime
