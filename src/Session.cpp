#include "Rudp/Session.hpp"

#include <random>

namespace Rudp::Session {
namespace {

[[nodiscard]] std::uint32_t generate_initial_seq() {
  std::random_device rd;
  const auto upper = static_cast<std::uint32_t>(rd()) << 16U;
  const auto lower = static_cast<std::uint32_t>(rd()) & 0xffffU;
  return upper ^ lower;
}

[[nodiscard]] std::uint32_t generate_conn_id() {
  std::random_device rd;
  std::uint32_t value = 0;
  while (value == 0) {
    const auto upper = static_cast<std::uint32_t>(rd()) << 16U;
    const auto lower = static_cast<std::uint32_t>(rd()) & 0xffffU;
    value = upper ^ lower;
  }
  return value;
}

void push_local_error_event(RxSessionState& rx, std::string error_message) {
  rx.pending_events.push_back(SessionEvent{
      .type = SessionEvent::Type::Error,
      .channel_id = 0,
      .channel_type = Rudp::ChannelType::Unreliable,
      .payload = {},
      .error_message = std::move(error_message),
  });
}

void push_control_event(RxSessionState& rx,
                        SessionEvent::Type type,
                        const Rudp::PacketView& packet,
                        std::string error_message = {}) {
  rx.pending_events.push_back(SessionEvent{
      .type = type,
      .channel_id = packet.header.channel_id,
      .channel_type = packet.header.channel_type,
      .payload = {},
      .error_message = std::move(error_message),
  });
}

}  // namespace

Session::Session(SessionRole role) : Session(role, generate_initial_seq()) {}

Session::Session(SessionRole role, std::uint32_t initial_seq)
    : state_(SessionState{
          .role = role,
          .conn_id = 0,
          .connection_state = ConnectionState::Closed,
          .tx =
              TxSessionState{
                  .next_seq = initial_seq,
                  .remote_ack = initial_seq,
                  .remote_ack_bits = 0,
                  .pending_send = {},
                  .inflight = {},
                  .syn_ack_pending = false,
                  .final_ack_pending = false,
                  .fin_pending = false,
                  .ack_only_pending = false,
              },
          .rx = {},
      }) {}

void Session::queue_send(std::uint32_t channel_id,
                         Rudp::ChannelType channel_type,
                         std::span<const std::byte> payload) {
  tx_handler_.queue_app_data(channel_id, channel_type, payload, state_.tx);
}

std::optional<std::vector<std::byte>> Session::poll_tx(std::uint64_t now_ms) {
  auto result =
      tx_handler_.poll(now_ms, state_.role, state_.conn_id,
                       state_.connection_state, state_.rx, state_.tx);
  if (result.fatal_error) {
    state_.connection_state = ConnectionState::Reset;
    push_local_error_event(state_.rx, std::move(result.error_message));
    return std::nullopt;
  }
  return result.datagram;
}

void Session::on_datagram_received(std::span<const std::byte> bytes,
                                   std::uint64_t now_ms) {
  const auto decoded = Rudp::Codec::decode(bytes);
  if (!decoded.has_value()) {
    return;
  }

  const auto control_kind = classify_control_kind(decoded->header);
  if (state_.role == SessionRole::Server && control_kind == ControlKind::Syn &&
      state_.conn_id == 0) {
    state_.conn_id = generate_conn_id();
  }
  if (state_.role == SessionRole::Client &&
      control_kind == ControlKind::SynAck && state_.conn_id == 0 &&
      decoded->header.conn_id != 0) {
    state_.conn_id = decoded->header.conn_id;
  }
  if (state_.conn_id != 0 && control_kind != ControlKind::Syn &&
      decoded->header.conn_id != state_.conn_id) {
    state_.connection_state = ConnectionState::Reset;
    push_local_error_event(state_.rx, "conn_id mismatch");
    return;
  }
  if (control_kind != ControlKind::Syn) {
    tx_handler_.on_remote_ack(decoded->header.ack, decoded->header.ack_bits,
                              state_.tx);
  }
  const auto decision = decide_connection_transition(
      state_.role, state_.connection_state, control_kind);
  apply_connection_decision(*decoded, decision);
  const auto rx_result =
      rx_handler_.on_packet(*decoded, now_ms, control_kind, state_.rx);

  if (rx_result.schedule_ack_only) {
    state_.tx.ack_only_pending = true;
  }
}

void Session::apply_connection_decision(const Rudp::PacketView& packet,
                                        const ConnectionDecision& decision) {
  if (!decision.valid) {
    if (!decision.error_message.empty()) {
      push_control_event(state_.rx, SessionEvent::Type::Error, packet,
                         decision.error_message);
    }
    return;
  }

  if (decision.next_state.has_value()) {
    state_.connection_state = *decision.next_state;
  }
  if (decision.schedule_syn_ack) {
    state_.tx.syn_ack_pending = true;
  }
  if (decision.schedule_final_ack) {
    state_.tx.final_ack_pending = true;
  }
  if (decision.schedule_ack_only) {
    state_.tx.ack_only_pending = true;
  }
  if (decision.emit_connected) {
    push_control_event(state_.rx, SessionEvent::Type::Connected, packet);
  }
  if (decision.emit_connection_closed) {
    push_control_event(state_.rx, SessionEvent::Type::ConnectionClosed, packet);
  }
  if (decision.emit_connection_reset) {
    push_control_event(state_.rx, SessionEvent::Type::ConnectionReset, packet);
  }
}

void Session::request_close() {
  if (state_.connection_state == ConnectionState::Established) {
    state_.connection_state = ConnectionState::Closing;
    state_.tx.fin_pending = true;
  }
}

std::vector<SessionEvent> Session::drain_events() {
  return rx_handler_.drain_events(state_.rx);
}

}  // namespace Rudp::Session
