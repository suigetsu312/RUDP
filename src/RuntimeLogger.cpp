#include "Rudp/RuntimeLogger.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <utility>

namespace Rudp::Runtime {
namespace {

[[nodiscard]] std::string to_string(Session::SessionEvent::Type type) {
  switch (type) {
    case Session::SessionEvent::Type::DataReceived:
      return "DataReceived";
    case Session::SessionEvent::Type::Connected:
      return "Connected";
    case Session::SessionEvent::Type::ConnectionClosed:
      return "ConnectionClosed";
    case Session::SessionEvent::Type::ConnectionReset:
      return "ConnectionReset";
    case Session::SessionEvent::Type::Error:
      return "Error";
  }
  return "Unknown";
}

}  // namespace

void log_line(const LogSink& logger, std::string_view message) {
  if (logger) {
    logger(message);
  }
}

LogSink make_console_logger() {
  return [](std::string_view message) { std::cout << message << '\n'; };
}

LogSink make_text_logger(std::string logger_name, std::string log_path) {
  std::filesystem::create_directories(
      std::filesystem::path(log_path).parent_path());
  auto logger = spdlog::basic_logger_mt(std::move(logger_name),
                                        std::move(log_path), true);
  logger->set_pattern("%Y-%m-%d %H:%M:%S.%e %v");
  logger->flush_on(spdlog::level::info);
  return [logger = std::move(logger)](std::string_view message) {
    logger->info("{}", message);
  };
}

LogSink compose_loggers(std::vector<LogSink> sinks) {
  return [sinks = std::move(sinks)](std::string_view message) {
    for (const auto& sink : sinks) {
      if (sink) {
        sink(message);
      }
    }
  };
}

std::string format_session_event(std::string_view prefix,
                                 const Session::SessionEvent& event) {
  std::string line(prefix);
  line += " event=" + to_string(event.type);
  line += " channel_id=" + std::to_string(event.channel_id);
  line += " payload_size=" + std::to_string(event.payload.size());
  if (!event.error_message.empty()) {
    line += " error=\"" + event.error_message + "\"";
  }
  if (!event.payload.empty()) {
    const auto text = std::string(
        reinterpret_cast<const char*>(event.payload.data()), event.payload.size());
    line += " payload=\"" + text + "\"";
  }
  return line;
}

std::string format_session_stats(const Session::SessionStats& stats) {
  std::string line;
  line += " sent=" + std::to_string(stats.packets_sent);
  line += " recv=" + std::to_string(stats.packets_received);
  line += " tx_bytes=" + std::to_string(stats.bytes_sent);
  line += " rx_bytes=" + std::to_string(stats.bytes_received);
  line += " ctrl_tx=" + std::to_string(stats.control_packets_sent);
  line += " ctrl_rx=" + std::to_string(stats.control_packets_received);
  line += " data_tx=" + std::to_string(stats.data_packets_sent);
  line += " data_rx=" + std::to_string(stats.data_packets_received);
  line += " ping=" + std::to_string(stats.pings_sent) + "/" +
          std::to_string(stats.pings_received);
  line += " pong=" + std::to_string(stats.pongs_sent) + "/" +
          std::to_string(stats.pongs_received);
  line += " retx=" + std::to_string(stats.retransmissions_sent);
  if (stats.latest_rtt_ms.has_value()) {
    line += " rtt_ms=" + std::to_string(*stats.latest_rtt_ms);
  }
  return line;
}

}  // namespace Rudp::Runtime
