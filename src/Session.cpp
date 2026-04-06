#include "Rudp/Session.hpp"

namespace Rudp::Session {
namespace {

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

void Session::queue_send(std::uint32_t channel_id,
                         Rudp::ChannelType channel_type,
                         std::span<const std::byte> payload) {
  tx_handler_.queue_app_data(channel_id, channel_type, payload, state_.tx);
}

std::optional<std::vector<std::byte>> Session::poll_tx(std::uint64_t now_ms) {
  return tx_handler_.poll(now_ms, state_.role, state_.connection_state,
                          state_.rx, state_.tx);
}

void Session::on_datagram_received(std::span<const std::byte> bytes,
                                   std::uint64_t now_ms) {
  const auto decoded = Rudp::Codec::decode(bytes);
  if (!decoded.has_value()) {
    return;
  }

  tx_handler_.on_remote_ack(decoded->header.ack, decoded->header.ack_bits,
                            state_.tx);
  const auto control_kind = classify_control_kind(decoded->header);
  const auto decision = decide_connection_transition(
      state_.role, state_.connection_state, control_kind);
  apply_connection_decision(*decoded, decision);
  rx_handler_.on_packet(*decoded, now_ms, control_kind, state_.rx);

  if (state_.rx.should_ack) {
    state_.tx.ack_only_pending = true;
    state_.rx.should_ack = false;
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
