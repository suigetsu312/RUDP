#include "Rudp/Session.hpp"

#include "Rudp/Config.hpp"

#include <algorithm>
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

[[nodiscard]] bool should_schedule_keepalive(const SessionState& state,
                                             std::uint64_t now_ms) {
  if (state.connection_state != ConnectionState::Established ||
      state.tx.ack_only_pending || state.tx.activity_ack_pending ||
      state.tx.ping_outstanding || state.tx.ping_pending ||
      state.tx.pong_pending) {
    return false;
  }

  const auto& transport = Rudp::Config::current().transport;
  const auto last_activity = std::max(state.last_rx_ms, state.last_tx_ms);
  return now_ms >= last_activity + transport.keepalive_idle_ms;
}

[[nodiscard]] bool should_schedule_activity_ack(const SessionState& state,
                                                std::uint64_t now_ms) {
  if (state.connection_state != ConnectionState::Established ||
      !state.tx.activity_ack_pending || state.tx.ack_only_pending ||
      state.tx.ping_pending || state.tx.pong_pending) {
    return false;
  }

  return now_ms >=
         state.last_tx_ms + Rudp::Config::current().transport.keepalive_idle_ms;
}

[[nodiscard]] bool should_timeout_idle_session(const SessionState& state,
                                               std::uint64_t now_ms) {
  if (state.connection_state != ConnectionState::Established) {
    return false;
  }

  return now_ms >=
         state.last_rx_ms + Rudp::Config::current().transport.idle_timeout_ms;
}

void record_received_stats(SessionState& state,
                           const Rudp::PacketView&,
                           ControlKind control_kind,
                           std::size_t datagram_size,
                           std::uint64_t now_ms) {
  state.last_rx_ms = now_ms;
  ++state.stats.packets_received;
  state.stats.bytes_received += datagram_size;

  if (control_kind != ControlKind::None) {
    ++state.stats.control_packets_received;
  } else {
    ++state.stats.data_packets_received;
  }

  if (control_kind == ControlKind::Ping) {
    ++state.stats.pings_received;
  } else if (control_kind == ControlKind::Pong) {
    ++state.stats.pongs_received;
  }
}

void record_outbound_stats(SessionState& state,
                           const Rudp::PacketView& packet,
                           std::size_t datagram_size,
                           std::uint64_t now_ms,
                           bool is_retransmission) {
  state.last_tx_ms = now_ms;
  ++state.stats.packets_sent;
  state.stats.bytes_sent += datagram_size;

  const auto control_kind = classify_control_kind(packet.header);
  if (control_kind != ControlKind::None) {
    ++state.stats.control_packets_sent;
  } else {
    ++state.stats.data_packets_sent;
  }

  if (control_kind == ControlKind::Ping) {
    ++state.stats.pings_sent;
  } else if (control_kind == ControlKind::Pong) {
    ++state.stats.pongs_sent;
  }

  if (is_retransmission) {
    ++state.stats.retransmissions_sent;
  }
}

void handle_keepalive_receive(SessionState& state,
                              ControlKind control_kind,
                              std::uint64_t now_ms) {
  if (control_kind == ControlKind::Ping &&
      state.connection_state == ConnectionState::Established) {
    state.tx.pong_pending = true;
    return;
  }

  if (control_kind == ControlKind::Pong && state.tx.ping_outstanding) {
    state.tx.ping_outstanding = false;
    state.stats.latest_rtt_ms = now_ms - state.tx.last_ping_sent_ms;
  }
}

}  // namespace

Session::Session(SessionRole role) : Session(role, generate_initial_seq()) {}

Session::Session(SessionRole role, std::uint32_t initial_seq)
    : state_(SessionState{
          .role = role,
          .conn_id = 0,
          .connection_state = ConnectionState::Closed,
          .last_rx_ms = 0,
          .last_tx_ms = 0,
          .stats = {},
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
                  .activity_ack_pending = false,
              },
          .rx = {},
      }) {}

void Session::queue_send(std::uint32_t channel_id,
                         Rudp::ChannelType channel_type,
                         std::span<const std::byte> payload) {
  tx_handler_.queue_app_data(channel_id, channel_type, payload, state_.tx);
}

std::optional<std::vector<std::byte>> Session::poll_tx(std::uint64_t now_ms) {
  if (should_timeout_idle_session(state_, now_ms)) {
    state_.connection_state = ConnectionState::Reset;
    emit_local_error(state_.rx, "idle timeout");
    return std::nullopt;
  }

  if (should_schedule_activity_ack(state_, now_ms)) {
    state_.tx.ack_only_pending = true;
  } else if (should_schedule_keepalive(state_, now_ms)) {
    state_.tx.ping_pending = true;
  }

  auto result =
      tx_handler_.poll(now_ms, state_.role, state_.conn_id,
                       state_.connection_state, state_.rx, state_.tx);
  if (result.fatal_error) {
    state_.connection_state = ConnectionState::Reset;
    emit_local_error(state_.rx, std::move(result.error_message));
    return std::nullopt;
  }
  if (result.datagram.has_value()) {
    const auto decoded = Rudp::Codec::decode(*result.datagram);
    if (decoded.has_value()) {
      const auto control_kind = classify_control_kind(decoded->header);
      if (control_kind == ControlKind::Ping) {
        state_.tx.ping_outstanding = true;
        state_.tx.last_ping_sent_ms = now_ms;
      }
      record_outbound_stats(state_, *decoded, result.datagram->size(), now_ms,
                            result.retransmission);
      state_.tx.activity_ack_pending = false;
    }
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
  record_received_stats(state_, *decoded, control_kind, bytes.size(), now_ms);

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

  handle_keepalive_receive(state_, control_kind, now_ms);

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

  if (control_kind == ControlKind::None &&
      state_.connection_state == ConnectionState::Established) {
    state_.tx.activity_ack_pending = true;
  }

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
