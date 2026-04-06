#include "Rudp/RxHandler.hpp"

namespace Rudp::Session {
namespace {

[[nodiscard]] OwnedPacket make_owned_packet(const Rudp::PacketView& packet) {
  return OwnedPacket{
      .header = packet.header,
      .payload =
          std::vector<std::byte>(packet.payload.begin(), packet.payload.end()),
  };
}

[[nodiscard]] SessionEvent make_data_event(const Rudp::PacketView& packet) {
  return SessionEvent{
      .type = SessionEvent::Type::DataReceived,
      .channel_id = packet.header.channel_id,
      .channel_type = packet.header.channel_type,
      .payload =
          std::vector<std::byte>(packet.payload.begin(), packet.payload.end()),
      .error_message = {},
  };
}

[[nodiscard]] bool is_control_only(ControlKind control_kind) {
  return control_kind != ControlKind::None;
}

void advance_receive_window(RxSessionState& rx) {
  ++rx.next_expected;

  while ((rx.received_bits & 1ULL) != 0ULL) {
    ++rx.next_expected;
    rx.received_bits >>= 1U;
  }
}

}  // namespace

void RxHandler::on_packet(const Rudp::PacketView& packet,
                          std::uint64_t now_ms,
                          ControlKind control_kind,
                          RxSessionState& rx) {
  static_cast<void>(now_ms);

  const bool should_process_payload =
      update_reliable_receive_state(packet, control_kind, rx);

  if (is_control_only(control_kind) || !should_process_payload) {
    return;
  }

  switch (packet.header.channel_type) {
    case Rudp::ChannelType::ReliableOrdered:
      handle_reliable_ordered(packet, rx);
      break;
    case Rudp::ChannelType::ReliableUnordered:
      handle_reliable_unordered(packet, rx);
      break;
    case Rudp::ChannelType::Unreliable:
      handle_unreliable(packet, rx);
      break;
    case Rudp::ChannelType::MonotonicState:
      handle_monotonic_state(packet, rx);
      break;
  }
}

std::vector<SessionEvent> RxHandler::drain_events(RxSessionState& rx) {
  std::vector<SessionEvent> events;
  events.swap(rx.pending_events);
  return events;
}

bool RxHandler::update_reliable_receive_state(const Rudp::PacketView& packet,
                                              ControlKind control_kind,
                                              RxSessionState& rx) {
  if (!Rudp::isReliableChannel(packet.header.channel_type) &&
      control_kind != ControlKind::Syn &&
      control_kind != ControlKind::SynAck &&
      control_kind != ControlKind::Ack &&
      control_kind != ControlKind::Fin) {
    return true;
  }

  rx.should_ack = true;

  if (rx.next_expected == 0) {
    rx.next_expected = packet.header.seq;
  }

  if (packet.header.seq == rx.next_expected) {
    advance_receive_window(rx);
    return true;
  }

  if (Rudp::seq_gt(packet.header.seq, rx.next_expected)) {
    const std::uint32_t delta = packet.header.seq - rx.next_expected;
    if (delta <= Rudp::kAckBitsWindow) {
      const std::uint64_t bit = (1ULL << (delta - 1U));
      const bool already_received = (rx.received_bits & bit) != 0ULL;
      rx.received_bits |= bit;
      return !already_received;
    }

    // Reliable packets outside the tracked ACK bitmap window cannot be
    // represented in current RX state, so they are not delivered yet.
    return false;
  }

  // Old reliable packets are stale for delivery purposes once the cumulative
  // ACK front has moved past them, so they are dropped here.
  return false;
}

void RxHandler::handle_reliable_ordered(const Rudp::PacketView& packet,
                                        RxSessionState& rx) {
  // PacketView is parse-time only and must not escape this function. Store an
  // owned copy if it needs to survive for reorder handling.
  rx.ordered_reorder_buffer[packet.header.seq] = make_owned_packet(packet);

  // TODO: implement ordered reliable reorder and contiguous drain.
}

void RxHandler::handle_reliable_unordered(const Rudp::PacketView& packet,
                                          RxSessionState& rx) {
  rx.pending_events.push_back(make_data_event(packet));

  // TODO: implement reliable unordered duplicate suppression policy.
}

void RxHandler::handle_unreliable(const Rudp::PacketView& packet,
                                  RxSessionState& rx) {
  rx.pending_events.push_back(make_data_event(packet));
}

void RxHandler::handle_monotonic_state(const Rudp::PacketView& packet,
                                       RxSessionState& rx) {
  if (packet.payload.size() < sizeof(std::uint32_t)) {
    return;
  }

  const std::uint32_t version =
      (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.payload[0])) << 24U) |
      (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.payload[1])) << 16U) |
      (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.payload[2])) << 8U) |
      static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(packet.payload[3]));

  const auto it = rx.monotonic_versions.find(packet.header.channel_id);
  if (it != rx.monotonic_versions.end()) {
    const auto current = it->second;
    if (static_cast<std::int32_t>(version - current) <= 0) {
      return;
    }
  }

  rx.monotonic_versions[packet.header.channel_id] = version;
  rx.pending_events.push_back(make_data_event(packet));
}

}  // namespace Rudp::Session
