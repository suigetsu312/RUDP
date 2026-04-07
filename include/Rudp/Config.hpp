#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Rudp::Config {

struct TransportSettings final {
  std::uint64_t initial_rto_ms = 250;
  std::uint64_t max_rto_ms = 4000;
  std::uint32_t max_retransmit_count = 5;
  std::uint64_t handshake_linger_ms = 1000;
  std::uint64_t keepalive_idle_ms = 5000;
  std::uint64_t idle_timeout_ms = 15000;
};

struct RuntimeSettings final {
  std::string server_bind_address = "127.0.0.1";
  std::uint16_t server_port = 9000;
  std::string client_server_address = "127.0.0.1";
  std::uint16_t client_server_port = 9000;
  std::string client_bind_address = "0.0.0.0";
  std::size_t socket_buffer_size = 1500;
  std::uint32_t server_loop_sleep_us = 10'000;
  std::uint32_t client_select_timeout_us = 10'000;
  std::uint32_t client_poll_budget = 8;
  std::string server_log_path = "logs/rudp_server.log";
  std::string client_log_path = "logs/rudp_client.log";
};

struct Settings final {
  TransportSettings transport;
  RuntimeSettings runtime;
};

[[nodiscard]] const Settings& current() noexcept;
[[nodiscard]] Settings& mutable_current() noexcept;
[[nodiscard]] bool load_from_env_file(
    const std::filesystem::path& path,
    std::string* error_message = nullptr);

}  // namespace Rudp::Config
