#include "Rudp/Config.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <ranges>
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

template <typename Integer>
bool assign_yaml_integer(Integer& target,
                         std::string_view raw_value,
                         std::string* error_message,
                         std::string_view key) {
  return assign_integer(target, raw_value, error_message, key);
}

[[nodiscard]] std::optional<RuntimeMode> parse_runtime_mode(
    std::string_view value) {
  if (value == "server") {
    return RuntimeMode::Server;
  }
  if (value == "client") {
    return RuntimeMode::Client;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Rudp::ChannelType> parse_channel_type(
    std::string_view value) {
  if (value == "reliable_ordered") {
    return Rudp::ChannelType::ReliableOrdered;
  }
  if (value == "reliable_unordered") {
    return Rudp::ChannelType::ReliableUnordered;
  }
  if (value == "unreliable") {
    return Rudp::ChannelType::Unreliable;
  }
  if (value == "monotonic_state") {
    return Rudp::ChannelType::MonotonicState;
  }
  return std::nullopt;
}

[[nodiscard]] RuntimeProfile default_runtime_profile() {
  const auto& runtime = g_settings.runtime;
  return RuntimeProfile{
      .mode = RuntimeMode::Server,
      .bind_address = runtime.server_bind_address,
      .bind_port = runtime.server_port,
      .remote_address = runtime.client_server_address,
      .remote_port = runtime.client_server_port,
      .log_path = runtime.server_log_path,
      .socket_buffer_size = runtime.socket_buffer_size,
      .loop_sleep_us = runtime.server_loop_sleep_us,
      .select_timeout_us = runtime.client_select_timeout_us,
      .poll_budget = runtime.client_poll_budget,
      .channels = {},
  };
}

[[nodiscard]] std::size_t indentation_of(std::string_view line) {
  std::size_t count = 0;
  while (count < line.size() && line[count] == ' ') {
    ++count;
  }
  return count;
}

[[nodiscard]] bool parse_bool(std::string_view value, bool& target) {
  if (value == "true") {
    target = true;
    return true;
  }
  if (value == "false") {
    target = false;
    return true;
  }
  return false;
}

[[nodiscard]] bool parse_key_value(std::string_view line,
                                   std::string& key,
                                   std::string& value) {
  const auto separator = line.find(':');
  if (separator == std::string_view::npos) {
    return false;
  }
  key = trim(line.substr(0, separator));
  value = unquote(trim(line.substr(separator + 1U)));
  return true;
}

bool apply_profile_field(RuntimeProfile& profile,
                         std::string_view scope,
                         std::string_view key,
                         std::string_view value,
                         std::string* error_message) {
  if (scope.empty()) {
    if (key == "mode") {
      const auto parsed_mode = parse_runtime_mode(value);
      if (!parsed_mode.has_value()) {
        if (error_message != nullptr) {
          *error_message = "mode must be server or client";
        }
        return false;
      }
      profile.mode = *parsed_mode;
      profile.log_path = profile.mode == RuntimeMode::Client
                             ? g_settings.runtime.client_log_path
                             : g_settings.runtime.server_log_path;
    }
    return true;
  }

  if (scope == "runtime") {
    if (key == "log_path") {
      profile.log_path = std::string(value);
      return true;
    }
    if (key == "socket_buffer_size") {
      return assign_yaml_integer(profile.socket_buffer_size, value,
                                 error_message, "runtime.socket_buffer_size");
    }
    if (key == "loop_sleep_us") {
      return assign_yaml_integer(profile.loop_sleep_us, value, error_message,
                                 "runtime.loop_sleep_us");
    }
    if (key == "select_timeout_us") {
      return assign_yaml_integer(profile.select_timeout_us, value,
                                 error_message, "runtime.select_timeout_us");
    }
    if (key == "poll_budget") {
      return assign_yaml_integer(profile.poll_budget, value, error_message,
                                 "runtime.poll_budget");
    }
    return true;
  }

  if (scope == "connection") {
    if (key == "bind_address") {
      profile.bind_address = std::string(value);
      return true;
    }
    if (key == "bind_port") {
      return assign_yaml_integer(profile.bind_port, value, error_message,
                                 "connection.bind_port");
    }
    if (key == "remote_address") {
      profile.remote_address = std::string(value);
      return true;
    }
    if (key == "remote_port") {
      return assign_yaml_integer(profile.remote_port, value, error_message,
                                 "connection.remote_port");
    }
    return true;
  }

  return true;
}

bool apply_channel_field(ChannelDefinition& channel,
                         std::string_view key,
                         std::string_view value,
                         std::string* error_message) {
  if (key == "id") {
    return assign_yaml_integer(channel.id, value, error_message,
                               "channels[].id");
  }
  if (key == "name") {
    channel.name = std::string(value);
    return true;
  }
  if (key == "type") {
    const auto parsed_type = parse_channel_type(value);
    if (!parsed_type.has_value()) {
      if (error_message != nullptr) {
        *error_message =
            "channel type must be reliable_ordered, reliable_unordered, "
            "unreliable, or monotonic_state";
      }
      return false;
    }
    channel.type = *parsed_type;
    return true;
  }
  if (key == "default") {
    if (!parse_bool(value, channel.is_default)) {
      if (error_message != nullptr) {
        *error_message = "channels[].default must be true or false";
      }
      return false;
    }
    return true;
  }
  return true;
}

bool apply_profile_yaml(RuntimeProfile& profile,
                        std::istream& input,
                        std::string* error_message) {
  std::string section;
  std::string line;
  std::size_t line_number = 0;
  ChannelDefinition* current_channel = nullptr;
  profile.channels.clear();

  while (std::getline(input, line)) {
    ++line_number;
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
      continue;
    }

    const auto indent = indentation_of(line);
    std::string key;
    std::string value;

    if (indent == 0U) {
      current_channel = nullptr;
      if (trimmed.ends_with(':')) {
        section = std::string(trimmed.substr(0, trimmed.size() - 1U));
        continue;
      }
      if (!parse_key_value(trimmed, key, value) ||
          !apply_profile_field(profile, "", key, value, error_message)) {
        if (error_message != nullptr && error_message->empty()) {
          *error_message = "invalid root entry on line " +
                           std::to_string(line_number);
        }
        return false;
      }
      continue;
    }

    if (section == "channels") {
      if (indent == 2U && trimmed.rfind("- ", 0) == 0) {
        profile.channels.push_back(ChannelDefinition{});
        current_channel = &profile.channels.back();
        if (!parse_key_value(trimmed.substr(2U), key, value) ||
            !apply_channel_field(*current_channel, key, value, error_message)) {
          if (error_message != nullptr && error_message->empty()) {
            *error_message = "invalid channel entry on line " +
                             std::to_string(line_number);
          }
          return false;
        }
        continue;
      }

      if (indent == 4U && current_channel != nullptr) {
        if (!parse_key_value(trimmed, key, value) ||
            !apply_channel_field(*current_channel, key, value, error_message)) {
          if (error_message != nullptr && error_message->empty()) {
            *error_message = "invalid channel field on line " +
                             std::to_string(line_number);
          }
          return false;
        }
        continue;
      }

      if (error_message != nullptr) {
        *error_message = "invalid channels indentation on line " +
                         std::to_string(line_number);
      }
      return false;
    }

    if (indent == 2U &&
        (section == "runtime" || section == "connection")) {
      if (!parse_key_value(trimmed, key, value) ||
          !apply_profile_field(profile, section, key, value, error_message)) {
        if (error_message != nullptr && error_message->empty()) {
          *error_message = "invalid " + section + " field on line " +
                           std::to_string(line_number);
        }
        return false;
      }
      continue;
    }

    if (error_message != nullptr) {
      *error_message = "unsupported YAML shape on line " +
                       std::to_string(line_number);
    }
    return false;
  }

  if (profile.channels.empty()) {
    profile.channels.push_back(ChannelDefinition{
        .id = 1U,
        .name = "default",
        .type = Rudp::ChannelType::Unreliable,
        .is_default = true,
    });
  } else if (std::ranges::none_of(
                 profile.channels, [](const ChannelDefinition& channel) {
                   return channel.is_default;
                 })) {
    profile.channels.front().is_default = true;
  }

  if (profile.mode == RuntimeMode::Server) {
    profile.remote_address.clear();
    profile.remote_port = 0;
  } else if (profile.remote_address.empty() || profile.remote_port == 0) {
    if (error_message != nullptr) {
      *error_message =
          "client profiles must define connection.remote_address and "
          "connection.remote_port";
    }
    return false;
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

bool load_runtime_profile_from_yaml(const std::filesystem::path& path,
                                    RuntimeProfile& profile,
                                    std::string* error_message) {
  std::ifstream input(path);
  if (!input.is_open()) {
    if (error_message != nullptr) {
      *error_message = "unable to open " + path.string();
    }
    return false;
  }

  RuntimeProfile loaded = default_runtime_profile();
  if (!apply_profile_yaml(loaded, input, error_message)) {
    return false;
  }
  profile = std::move(loaded);
  return true;
}

}  // namespace Rudp::Config
