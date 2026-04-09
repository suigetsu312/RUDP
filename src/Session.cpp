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
      .seq = 0,
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
      .seq = packet.header.seq,
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

[[nodiscard]] bool should_schedule_probe(const SessionState& state,
                                         std::uint64_t now_ms) {
  if (state.role != SessionRole::Client ||
      state.connection_state != ConnectionState::Established ||
      state.tx.probe.ping_outstanding || state.tx.probe.ping_pending ||
      state.tx.probe.pong_pending) {
    return false;
  }

  const auto& transport = Rudp::Config::current().transport;
  const auto probe_baseline =
      state.tx.probe.last_sent_ms != 0 ? state.tx.probe.last_sent_ms
                                       : state.established_since_ms;
  return now_ms >= probe_baseline + transport.keepalive_idle_ms;
}

[[nodiscard]] bool should_schedule_activity_ack(const SessionState& state,
                                                std::uint64_t now_ms) {
  if (!Rudp::Config::current().transport.enable_activity_ack_only) {
    return false;
  }

  if (state.connection_state != ConnectionState::Established ||
      !state.tx.activity_ack_pending || state.tx.ack_only_pending ||
      state.tx.probe.ping_pending || state.tx.probe.pong_pending) {
    return false;
  }

  return now_ms >=
         state.last_tx_ms + Rudp::Config::current().transport.keepalive_idle_ms;
}

[[nodiscard]] bool should_schedule_reliable_ack(const SessionState& state,
                                                std::uint64_t now_ms) {
  if (state.connection_state != ConnectionState::Established ||
      !state.tx.reliable_ack_pending || state.tx.ack_only_pending ||
      state.tx.probe.ping_pending || state.tx.probe.pong_pending) {
    return false;
  }

  return now_ms >= state.tx.reliable_ack_due_ms;
}

[[nodiscard]] bool should_timeout_idle_session(const SessionState& state,
                                               std::uint64_t now_ms) {
  if (state.connection_state != ConnectionState::Established) {
    return false;
  }

  return now_ms >=
         state.last_rx_ms + Rudp::Config::current().transport.idle_timeout_ms;
}

void mark_idle_timeout(SessionState& state) {
  state.connection_state = ConnectionState::Reset;
  emit_local_error(state.rx, "idle timeout");
}

void schedule_pending_tx_work(SessionState& state, std::uint64_t now_ms) {
  if (should_schedule_reliable_ack(state, now_ms)) {
    state.tx.ack_only_pending = true;
    state.tx.reliable_ack_pending = false;
    return;
  }

  if (should_schedule_activity_ack(state, now_ms)) {
    state.tx.ack_only_pending = true;
    return;
  }

  if (should_schedule_probe(state, now_ms)) {
    state.tx.probe.ping_pending = true;
  }
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

void update_outbound_probe_state(SessionState& state,
                                 ControlKind control_kind,
                                 std::uint64_t now_ms) {
  if (control_kind != ControlKind::Ping) {
    return;
  }

  state.tx.probe.ping_outstanding = true;
  state.tx.probe.last_sent_ms = now_ms;
  state.tx.probe.last_ping_sent_ms = now_ms;
}

void clear_outbound_ack_state(SessionState& state, ControlKind control_kind) {
  state.tx.activity_ack_pending = false;
  if (control_kind == ControlKind::Ack) {
    state.tx.reliable_ack_pending = false;
  }
}

void apply_outbound_result(SessionState& state,
                           const std::vector<std::byte>& datagram,
                           std::uint64_t now_ms,
                           bool is_retransmission) {
  const auto decoded = Rudp::Codec::decode(datagram);
  if (!decoded.has_value()) {
    return;
  }

  const auto control_kind = classify_control_kind(decoded->header);
  update_outbound_probe_state(state, control_kind, now_ms);
  record_outbound_stats(state, *decoded, datagram.size(), now_ms,
                        is_retransmission);
  clear_outbound_ack_state(state, control_kind);
}

void handle_probe_receive(SessionState& state,
                          ControlKind control_kind,
                          std::uint64_t now_ms) {
  if (control_kind == ControlKind::Ping &&
      state.connection_state == ConnectionState::Established) {
    state.tx.probe.pong_pending = true;
    return;
  }

  if (control_kind == ControlKind::Pong && state.tx.probe.ping_outstanding) {
    state.tx.probe.ping_outstanding = false;
    const auto rtt_ms = now_ms - state.tx.probe.last_ping_sent_ms;
    state.stats.latest_rtt_ms = rtt_ms;
    ++state.stats.rtt_sample_count;
    state.stats.rtt_sum_ms += rtt_ms;
    if (!state.stats.min_rtt_ms.has_value() ||
        rtt_ms < *state.stats.min_rtt_ms) {
      state.stats.min_rtt_ms = rtt_ms;
    }
    if (!state.stats.max_rtt_ms.has_value() ||
        rtt_ms > *state.stats.max_rtt_ms) {
      state.stats.max_rtt_ms = rtt_ms;
    }
  }
}

void update_post_receive_liveness(SessionState& state, ControlKind control_kind) {
  if (control_kind == ControlKind::None &&
      state.connection_state == ConnectionState::Established) {
    state.tx.activity_ack_pending = true;
  }
}

void schedule_receive_side_ack(SessionState& state,
                               const Rudp::PacketView& packet,
                               ControlKind control_kind,
                               const RxPacketResult& rx_result,
                               std::uint64_t now_ms) {
  if (!rx_result.schedule_ack_only) {
    return;
  }

  if (control_kind == ControlKind::None &&
      Rudp::isReliableChannel(packet.header.channel_type)) {
    state.tx.reliable_ack_pending = true;
    state.tx.reliable_ack_due_ms =
        now_ms + Rudp::Config::current().transport.reliable_ack_delay_ms;
    return;
  }

  state.tx.ack_only_pending = true;
}

}  // namespace

Session::Session(SessionRole role) : Session(role, generate_initial_seq()) {}

Session::Session(SessionRole role, std::uint32_t initial_seq)
    : state_(SessionState{
          .role = role,
          .conn_id = 0,
          .connection_state = ConnectionState::Closed,
          .established_since_ms = 0,
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
                  .reliable_ack_pending = false,
                  .reliable_ack_due_ms = 0,
                  .activity_ack_pending = false,
                  .probe = {},
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
    mark_idle_timeout(state_);
    return std::nullopt;
  }

  schedule_pending_tx_work(state_, now_ms);

  auto result =
      tx_handler_.poll(now_ms, state_.role, state_.conn_id,
                       state_.connection_state, state_.rx, state_.tx);
  if (result.fatal_error) {
    state_.connection_state = ConnectionState::Reset;
    emit_local_error(state_.rx, std::move(result.error_message));
    return std::nullopt;
  }
  if (result.datagram.has_value()) {
    apply_outbound_result(state_, *result.datagram, now_ms,
                          result.retransmission);
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

  handle_probe_receive(state_, control_kind, now_ms);

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

  update_post_receive_liveness(state_, control_kind);
  schedule_receive_side_ack(state_, *decoded, control_kind, rx_result, now_ms);
}

void Session::apply_connection_decision(const Rudp::PacketView& packet,
                                        const ConnectionDecision& decision) {
  const auto previous_state = state_.connection_state;
  if (!decision.valid) {
    if (!decision.error_message.empty()) {
      emit_control_event(state_.rx, SessionEvent::Type::Error, packet,
                         decision.error_message);
    }
    return;
  }

  if (decision.next_state.has_value()) {
    state_.connection_state = *decision.next_state;
    if (previous_state != ConnectionState::Established &&
        state_.connection_state == ConnectionState::Established) {
      state_.established_since_ms = state_.last_rx_ms;
    }
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
