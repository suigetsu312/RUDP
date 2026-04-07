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

void emit_local_error(RxSessionState& rx, std::string error_message) {
  rx.pending_events.push_back(SessionEvent{
      .type = SessionEvent::Type::Error,
      .channel_id = 0,
      .channel_type = Rudp::ChannelType::Unreliable,
      .payload = {},
      .error_message = std::move(error_message),
  });
}

void emit_control_event(RxSessionState& rx,
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

[[nodiscard]] bool should_adopt_server_conn_id(
    const SessionState& state,
    ControlKind control_kind,
    const Rudp::Header& header) {
  return state.role == SessionRole::Client &&
         control_kind == ControlKind::SynAck && state.conn_id == 0 &&
         header.conn_id != 0;
}

[[nodiscard]] bool allows_server_zero_conn_id_bootstrap(
    const SessionState& state,
    const Rudp::Header& header) {
  return state.role == SessionRole::Server &&
         state.connection_state == ConnectionState::Closed &&
         header.conn_id == 0;
}

[[nodiscard]] bool has_conn_id_mismatch(const SessionState& state,
                                        ControlKind control_kind,
                                        const Rudp::Header& header) {
  return state.conn_id != 0 && control_kind != ControlKind::Syn &&
         !allows_server_zero_conn_id_bootstrap(state, header) &&
         header.conn_id != state.conn_id;
}

[[nodiscard]] bool is_duplicate_client_syn_ack(const SessionState& state,
                                               ControlKind control_kind) {
  return state.role == SessionRole::Client &&
         state.connection_state == ConnectionState::Established &&
         control_kind == ControlKind::SynAck;
}

void adopt_server_conn_id(SessionState& state, const Rudp::Header& header) {
  state.conn_id = header.conn_id;
}

void reset_for_conn_id_mismatch(SessionState& state) {
  state.connection_state = ConnectionState::Reset;
  emit_local_error(state.rx, "conn_id mismatch");
}

void schedule_final_ack_during_linger(SessionState& state,
                                      std::uint64_t now_ms) {
  if (now_ms <= state.tx.final_ack_linger_until_ms) {
    state.tx.final_ack_pending = true;
  }
}

[[nodiscard]] bool should_apply_remote_ack(ControlKind control_kind) {
  return control_kind != ControlKind::Syn;
}

void apply_remote_ack(TxHandler& tx_handler,
                      const Rudp::Header& header,
                      ControlKind control_kind,
                      TxAckResult& ack_result,
                      TxSessionState& tx) {
  if (!should_apply_remote_ack(control_kind)) {
    return;
  }

  ack_result = tx_handler.on_remote_ack(header.ack, header.ack_bits, tx);
}

[[nodiscard]] bool should_close_after_fin_acknowledgement(
    const SessionState& state,
    const TxAckResult& ack_result) {
  return ack_result.acknowledged_fin &&
         state.connection_state == ConnectionState::Closing;
}

void close_after_fin_acknowledgement(SessionState& state,
                                     const Rudp::PacketView& packet) {
  state.connection_state = ConnectionState::Closed;
  emit_control_event(state.rx, SessionEvent::Type::ConnectionClosed, packet);
}

void emit_connection_decision_events(RxSessionState& rx,
                                     const Rudp::PacketView& packet,
                                     const ConnectionDecision& decision) {
  if (decision.emit_connected) {
    emit_control_event(rx, SessionEvent::Type::Connected, packet);
  }
  if (decision.emit_connection_closed) {
    emit_control_event(rx, SessionEvent::Type::ConnectionClosed, packet);
  }
  if (decision.emit_connection_reset) {
    emit_control_event(rx, SessionEvent::Type::ConnectionReset, packet);
  }
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
                  .final_ack_linger_until_ms = 0,
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
    emit_local_error(state_.rx, std::move(result.error_message));
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
  if (should_adopt_server_conn_id(state_, control_kind, decoded->header)) {
    adopt_server_conn_id(state_, decoded->header);
  }

  if (has_conn_id_mismatch(state_, control_kind, decoded->header)) {
    reset_for_conn_id_mismatch(state_);
    return;
  }

  if (is_duplicate_client_syn_ack(state_, control_kind)) {
    schedule_final_ack_during_linger(state_, now_ms);
    return;
  }

  TxAckResult ack_result{};
  apply_remote_ack(tx_handler_, decoded->header, control_kind, ack_result,
                   state_.tx);
  if (should_close_after_fin_acknowledgement(state_, ack_result)) {
    close_after_fin_acknowledgement(state_, *decoded);
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
      emit_control_event(state_.rx, SessionEvent::Type::Error, packet,
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
  emit_connection_decision_events(state_.rx, packet, decision);
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
