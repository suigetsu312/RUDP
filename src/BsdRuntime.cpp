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

void apply_log_path_override(Rudp::Config::RuntimeProfile& profile) {
  const char* override_path = nullptr;
  if (profile.mode == Rudp::Config::RuntimeMode::Server) {
    override_path = std::getenv("RUDP_RUNTIME_SERVER_LOG_PATH");
  } else {
    override_path = std::getenv("RUDP_RUNTIME_CLIENT_LOG_PATH");
  }

  if (override_path != nullptr && *override_path != '\0') {
    profile.log_path = override_path;
  }
}

void print_usage(std::string_view argv0) {
  std::cerr << "Usage:\n"
            << "  " << argv0 << " <runtime-config.yaml>\n"
            << "Transport defaults are loaded from .env when present.\n";
}

[[nodiscard]] std::optional<std::filesystem::path> parse_args(int argc,
                                                              char** argv) {
  if (argc != 2) {
    print_usage(argv[0]);
    return std::nullopt;
  }
  return std::filesystem::path(argv[1]);
}

[[nodiscard]] LogSink make_runtime_logger(
    const Rudp::Config::RuntimeProfile& profile) {
  std::vector<LogSink> sinks;
  sinks.push_back(make_console_logger());

  try {
    const auto logger_name = profile.mode == Rudp::Config::RuntimeMode::Server
                                 ? "rudp_server_file"
                                 : "rudp_client_file";
    sinks.push_back(make_text_logger(logger_name, profile.log_path));
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

  const auto config_path = parse_args(argc, argv);
  if (!config_path.has_value()) {
    return 1;
  }

  Rudp::Config::RuntimeProfile profile;
  std::string profile_error;
  if (!Rudp::Config::load_runtime_profile_from_yaml(*config_path, profile,
                                                    &profile_error)) {
    std::cerr << "failed to load runtime config: " << profile_error << '\n';
    return 1;
  }
  apply_log_path_override(profile);

  const auto logger = make_runtime_logger(profile);
  if (profile.mode == Rudp::Config::RuntimeMode::Server) {
    run_server_app(profile, logger);
  } else {
    run_client_app(profile, logger);
  }
  return 0;
}

}  // namespace Rudp::Runtime
