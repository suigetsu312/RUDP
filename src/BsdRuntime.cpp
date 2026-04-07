#include "Rudp/BsdRuntime.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Rudp/BsdClientApp.hpp"
#include "Rudp/BsdServerApp.hpp"
#include "Rudp/Config.hpp"
#include "Rudp/RuntimeLogger.hpp"

namespace Rudp::Runtime {
namespace {

struct CliConfig final {
  enum class Mode {
    Server,
    Client,
  };

  Mode mode = Mode::Server;
  std::string address;
  std::uint16_t port = 0;
};

[[nodiscard]] bool parse_mode(std::string_view value, CliConfig::Mode& mode) {
  if (value == "server") {
    mode = CliConfig::Mode::Server;
    return true;
  }
  if (value == "client") {
    mode = CliConfig::Mode::Client;
    return true;
  }
  return false;
}

void print_usage(std::string_view argv0) {
  std::cerr << "Usage:\n"
            << "  " << argv0 << " server [bind-address] [port]\n"
            << "  " << argv0 << " client [server-address] [port]\n"
            << "Defaults are loaded from .env when present.\n";
}

[[nodiscard]] std::optional<CliConfig> parse_args(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    print_usage(argv[0]);
    return std::nullopt;
  }

  CliConfig config;
  if (!parse_mode(argv[1], config.mode)) {
    print_usage(argv[0]);
    return std::nullopt;
  }

  const auto& runtime = Rudp::Config::current().runtime;
  if (config.mode == CliConfig::Mode::Server) {
    config.address = runtime.server_bind_address;
    config.port = runtime.server_port;
  } else {
    config.address = runtime.client_server_address;
    config.port = runtime.client_server_port;
  }

  if (argc >= 3) {
    config.address = argv[2];
  }
  if (argc >= 4) {
    const auto parsed_port = std::strtol(argv[3], nullptr, 10);
    if (parsed_port <= 0 || parsed_port > 65535) {
      std::cerr << "Invalid port: " << argv[3] << '\n';
      return std::nullopt;
    }
    config.port = static_cast<std::uint16_t>(parsed_port);
  }

  return config;
}

[[nodiscard]] std::string default_log_filename(const CliConfig& config) {
  const auto& runtime = Rudp::Config::current().runtime;
  return config.mode == CliConfig::Mode::Server ? runtime.server_log_path
                                                : runtime.client_log_path;
}

[[nodiscard]] LogSink make_runtime_logger(const CliConfig& config) {
  std::vector<LogSink> sinks;
  sinks.push_back(make_console_logger());

  try {
    const auto logger_name = config.mode == CliConfig::Mode::Server
                                 ? "rudp_server_file"
                                 : "rudp_client_file";
    sinks.push_back(
        make_text_logger(logger_name, default_log_filename(config)));
  } catch (const spdlog::spdlog_ex& error) {
    std::cerr << "failed to create file logger: " << error.what() << '\n';
  }

  return compose_loggers(std::move(sinks));
}

}  // namespace

int run_cli(int argc, char** argv) {
  std::string env_error;
  if (std::filesystem::exists(".env") &&
      !Rudp::Config::load_from_env_file(".env", &env_error)) {
    std::cerr << "failed to load .env: " << env_error << '\n';
    return 1;
  }

  const auto config = parse_args(argc, argv);
  if (!config.has_value()) {
    return 1;
  }

  const auto logger = make_runtime_logger(*config);
  if (config->mode == CliConfig::Mode::Server) {
    run_server_app(config->address, config->port, logger);
  } else {
    run_client_app(config->address, config->port, logger);
  }
  return 0;
}

}  // namespace Rudp::Runtime
