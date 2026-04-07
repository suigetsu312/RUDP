#include "Rudp/Config.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <string_view>
#include <utility>

namespace Rudp::Config {
namespace {

Settings g_settings{};

[[nodiscard]] std::string trim(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }

  std::size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
    --last;
  }

  return std::string(value.substr(first, last - first));
}

[[nodiscard]] std::string unquote(std::string_view value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return std::string(value.substr(1, value.size() - 2));
  }
  return std::string(value);
}

template <typename Integer>
bool assign_integer(Integer& target,
                    std::string_view raw_value,
                    std::string* error_message,
                    std::string_view key) {
  try {
    const auto parsed = std::stoull(std::string(raw_value));
    target = static_cast<Integer>(parsed);
    return true;
  } catch (const std::exception&) {
    if (error_message != nullptr) {
      *error_message = "invalid integer for key " + std::string(key);
    }
    return false;
  }
}

bool apply_kv(Settings& settings,
              std::string_view key,
              std::string_view value,
              std::string* error_message) {
  if (key == "RUDP_TRANSPORT_INITIAL_RTO_MS") {
    return assign_integer(settings.transport.initial_rto_ms, value,
                          error_message, key);
  }
  if (key == "RUDP_TRANSPORT_MAX_RTO_MS") {
    return assign_integer(settings.transport.max_rto_ms, value, error_message,
                          key);
  }
  if (key == "RUDP_TRANSPORT_MAX_RETRANSMIT_COUNT") {
    return assign_integer(settings.transport.max_retransmit_count, value,
                          error_message, key);
  }
  if (key == "RUDP_TRANSPORT_HANDSHAKE_LINGER_MS") {
    return assign_integer(settings.transport.handshake_linger_ms, value,
                          error_message, key);
  }
  if (key == "RUDP_TRANSPORT_KEEPALIVE_IDLE_MS") {
    return assign_integer(settings.transport.keepalive_idle_ms, value,
                          error_message, key);
  }
  if (key == "RUDP_TRANSPORT_IDLE_TIMEOUT_MS") {
    return assign_integer(settings.transport.idle_timeout_ms, value,
                          error_message, key);
  }
  if (key == "RUDP_RUNTIME_SERVER_BIND_ADDRESS") {
    settings.runtime.server_bind_address = unquote(value);
    return true;
  }
  if (key == "RUDP_RUNTIME_SERVER_PORT") {
    return assign_integer(settings.runtime.server_port, value, error_message,
                          key);
  }
  if (key == "RUDP_RUNTIME_CLIENT_SERVER_ADDRESS") {
    settings.runtime.client_server_address = unquote(value);
    return true;
  }
  if (key == "RUDP_RUNTIME_CLIENT_SERVER_PORT") {
    return assign_integer(settings.runtime.client_server_port, value,
                          error_message, key);
  }
  if (key == "RUDP_RUNTIME_CLIENT_BIND_ADDRESS") {
    settings.runtime.client_bind_address = unquote(value);
    return true;
  }
  if (key == "RUDP_RUNTIME_SOCKET_BUFFER_SIZE") {
    return assign_integer(settings.runtime.socket_buffer_size, value,
                          error_message, key);
  }
  if (key == "RUDP_RUNTIME_SERVER_LOOP_SLEEP_US") {
    return assign_integer(settings.runtime.server_loop_sleep_us, value,
                          error_message, key);
  }
  if (key == "RUDP_RUNTIME_CLIENT_SELECT_TIMEOUT_US") {
    return assign_integer(settings.runtime.client_select_timeout_us, value,
                          error_message, key);
  }
  if (key == "RUDP_RUNTIME_CLIENT_POLL_BUDGET") {
    return assign_integer(settings.runtime.client_poll_budget, value,
                          error_message, key);
  }
  if (key == "RUDP_RUNTIME_SERVER_LOG_PATH") {
    settings.runtime.server_log_path = unquote(value);
    return true;
  }
  if (key == "RUDP_RUNTIME_CLIENT_LOG_PATH") {
    settings.runtime.client_log_path = unquote(value);
    return true;
  }
  return true;
}

}  // namespace

const Settings& current() noexcept { return g_settings; }

Settings& mutable_current() noexcept { return g_settings; }

bool load_from_env_file(const std::filesystem::path& path,
                        std::string* error_message) {
  std::ifstream input(path);
  if (!input.is_open()) {
    if (error_message != nullptr) {
      *error_message = "unable to open " + path.string();
    }
    return false;
  }

  Settings loaded = g_settings;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    const auto separator = trimmed.find('=');
    if (separator == std::string::npos) {
      if (error_message != nullptr) {
        *error_message = "invalid .env line " + std::to_string(line_number);
      }
      return false;
    }

    const auto key = trim(std::string_view(trimmed).substr(0, separator));
    const auto value =
        trim(std::string_view(trimmed).substr(separator + 1U));
    if (!apply_kv(loaded, key, value, error_message)) {
      return false;
    }
  }

  g_settings = std::move(loaded);
  return true;
}

}  // namespace Rudp::Config
