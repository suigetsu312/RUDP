#include "Rudp/ConnectionStateMachine.hpp"

namespace Rudp::Session {
namespace {

[[nodiscard]] ConnectionDecision make_invalid_decision(std::string message) {
  return ConnectionDecision{
      .valid = false,
      .next_state = std::nullopt,
      .schedule_syn_ack = false,
      .schedule_final_ack = false,
      .schedule_ack_only = false,
      .emit_connected = false,
      .emit_connection_closed = false,
      .emit_connection_reset = false,
      .error_message = std::move(message),
  };
}

}  // namespace

ControlKind classify_control_kind(const Rudp::Header& header) noexcept {
  const bool syn = header.hasFlag(Rudp::Flag::Syn);
  const bool ack = header.hasFlag(Rudp::Flag::Ack);
  const bool fin = header.hasFlag(Rudp::Flag::Fin);
  const bool rst = header.hasFlag(Rudp::Flag::Rst);
  const bool ping = header.hasFlag(Rudp::Flag::Ping);
  const bool pong = header.hasFlag(Rudp::Flag::Pong);

  const unsigned flag_count = static_cast<unsigned>(syn) +
                              static_cast<unsigned>(fin) +
                              static_cast<unsigned>(rst) +
                              static_cast<unsigned>(ping) +
                              static_cast<unsigned>(pong);

  if (ack) {
    if (syn && !fin && !rst && !ping && !pong) {
      return ControlKind::SynAck;
    }
    if (!syn && !fin && !rst && !ping && !pong) {
      return ControlKind::Ack;
    }
    return ControlKind::Invalid;
  }

  if (flag_count == 0U) {
    return ControlKind::None;
  }
  if (flag_count > 1U) {
    return ControlKind::Invalid;
  }
  if (syn) {
    return ControlKind::Syn;
  }
  if (fin) {
    return ControlKind::Fin;
  }
  if (rst) {
    return ControlKind::Rst;
  }
  if (ping) {
    return ControlKind::Ping;
  }
  if (pong) {
    return ControlKind::Pong;
  }
  return ControlKind::Invalid;
}

ConnectionDecision decide_connection_transition(SessionRole role,
                                               ConnectionState current_state,
                                               ControlKind kind) {
  switch (kind) {
    case ControlKind::None:
    case ControlKind::Ping:
    case ControlKind::Pong:
      return {};
    case ControlKind::Invalid:
      return make_invalid_decision("invalid control-flag combination");
    case ControlKind::Rst:
      return ConnectionDecision{
          .valid = true,
          .next_state = ConnectionState::Reset,
          .schedule_syn_ack = false,
          .schedule_final_ack = false,
          .schedule_ack_only = false,
          .emit_connected = false,
          .emit_connection_closed = false,
          .emit_connection_reset = true,
          .error_message = {},
      };
    case ControlKind::Fin:
      return ConnectionDecision{
          .valid = true,
          .next_state = ConnectionState::Closing,
          .schedule_syn_ack = false,
          .schedule_final_ack = false,
          .schedule_ack_only = true,
          .emit_connected = false,
          .emit_connection_closed = true,
          .emit_connection_reset = false,
          .error_message = {},
      };
    case ControlKind::Syn:
      if (role == SessionRole::Server &&
          current_state == ConnectionState::Closed) {
        return ConnectionDecision{
            .valid = true,
            .next_state = ConnectionState::HandshakeReceived,
            .schedule_syn_ack = true,
            .schedule_final_ack = false,
            .schedule_ack_only = false,
            .emit_connected = false,
            .emit_connection_closed = false,
            .emit_connection_reset = false,
            .error_message = {},
        };
      }
      if (current_state == ConnectionState::Established) {
        return make_invalid_decision(
            "unexpected SYN in Established state");
      }
      return {};
    case ControlKind::SynAck:
      if (role == SessionRole::Client &&
          current_state == ConnectionState::HandshakeSent) {
        return ConnectionDecision{
            .valid = true,
            .next_state = ConnectionState::Established,
            .schedule_syn_ack = false,
            .schedule_final_ack = true,
            .schedule_ack_only = false,
            .emit_connected = true,
            .emit_connection_closed = false,
            .emit_connection_reset = false,
            .error_message = {},
        };
      }
      if (role == SessionRole::Client &&
          current_state == ConnectionState::Established) {
        return ConnectionDecision{
            .valid = true,
            .next_state = std::nullopt,
            .schedule_syn_ack = false,
            .schedule_final_ack = true,
            .schedule_ack_only = false,
            .emit_connected = false,
            .emit_connection_closed = false,
            .emit_connection_reset = false,
            .error_message = {},
        };
      }
      return {};
    case ControlKind::Ack:
      if (role == SessionRole::Server &&
          current_state == ConnectionState::HandshakeReceived) {
        return ConnectionDecision{
            .valid = true,
            .next_state = ConnectionState::Established,
            .schedule_syn_ack = false,
            .schedule_final_ack = false,
            .schedule_ack_only = false,
            .emit_connected = true,
            .emit_connection_closed = false,
            .emit_connection_reset = false,
            .error_message = {},
        };
      }
      return {};
  }

  return {};
}

}  // namespace Rudp::Session
