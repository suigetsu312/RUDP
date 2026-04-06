#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Rudp/SessionTypes.hpp"

namespace Rudp::Session {

enum class ControlKind : std::uint8_t {
  None = 0,
  Syn = 1,
  SynAck = 2,
  Ack = 3,
  Fin = 4,
  Rst = 5,
  Ping = 6,
  Pong = 7,
  Invalid = 8,
};

struct ConnectionDecision final {
  bool valid = true;
  std::optional<ConnectionState> next_state;
  bool schedule_syn_ack = false;
  bool schedule_final_ack = false;
  bool schedule_ack_only = false;
  bool emit_connected = false;
  bool emit_connection_closed = false;
  bool emit_connection_reset = false;
  std::string error_message;
};

[[nodiscard]] ControlKind classify_control_kind(
    const Rudp::Header& header) noexcept;

[[nodiscard]] ConnectionDecision decide_connection_transition(
    SessionRole role,
    ConnectionState current_state,
    ControlKind kind);

}  // namespace Rudp::Session
