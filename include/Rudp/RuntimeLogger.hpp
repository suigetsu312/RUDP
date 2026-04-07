#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Rudp/ServerSessionManager.hpp"
#include "Rudp/Session.hpp"

namespace Rudp::Runtime {

using LogSink = std::function<void(std::string_view)>;

void log_line(const LogSink& logger, std::string_view message);

[[nodiscard]] LogSink make_console_logger();
[[nodiscard]] LogSink make_text_logger(std::string logger_name,
                                       std::string log_path);
[[nodiscard]] LogSink compose_loggers(std::vector<LogSink> sinks);
[[nodiscard]] std::string format_session_event(
    std::string_view prefix,
    const Session::SessionEvent& event);
[[nodiscard]] std::string format_session_stats(
    const Session::SessionStats& stats);

}  // namespace Rudp::Runtime
