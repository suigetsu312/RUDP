#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "Rudp/RxHandler.hpp"

namespace {

using Rudp::Session::ControlKind;
using Rudp::Session::RxHandler;
using Rudp::Session::RxSessionState;
using Rudp::Session::SessionEvent;

[[nodiscard]] Rudp::PacketView make_packet_view(
    Rudp::Header header,
    std::span<const std::byte> payload = {}) {
  return Rudp::PacketView{
      .header = header,
      .payload = payload,
  };
}

// Verifies the ACK front advances correctly across uint32 wrap-around when the
// next in-order packet is the maximum sequence value.
TEST(RxHandlerWrapTest, InOrderPacketAtUint32MaxAdvancesAcrossWrapBoundary) {
  RxHandler handler;
  RxSessionState rx;
  rx.next_expected = 0xffffffffu;
  rx.received_bits = 0x1ULL;

  Rudp::Header header;
  header.seq = 0xffffffffu;
  header.channel_id = 1U;
  header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x01}};
  static_cast<void>(
      handler.on_packet(make_packet_view(header, payload), 100U,
                        ControlKind::None, rx));

  EXPECT_EQ(rx.next_expected, 1U);
  EXPECT_EQ(rx.received_bits, 0ULL);

  const auto events = handler.drain_events(rx);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::DataReceived);
}

// Verifies a future reliable packet that wraps from uint32 max to zero is
// represented in the ACK bitmap and delivered once for unordered channels.
TEST(RxHandlerWrapTest, FutureReliablePacketAtZeroIsTrackedAcrossWrapBoundary) {
  RxHandler handler;
  RxSessionState rx;
  rx.next_expected = 0xffffffffu;

  Rudp::Header header;
  header.seq = 0u;
  header.channel_id = 2U;
  header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x02}};
  static_cast<void>(
      handler.on_packet(make_packet_view(header, payload), 100U,
                        ControlKind::None, rx));

  EXPECT_EQ(rx.next_expected, 0xffffffffu);
  EXPECT_EQ(rx.received_bits, 0x1ULL);

  const auto events = handler.drain_events(rx);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::DataReceived);
  EXPECT_EQ(events.front().channel_id, 2U);
}

// Verifies a packet that falls behind the ACK front after wrap-around is
// treated as stale and is not delivered again.
TEST(RxHandlerWrapTest, StaleReliablePacketAfterWrapBoundaryIsDropped) {
  RxHandler handler;
  RxSessionState rx;
  rx.next_expected = 1U;

  Rudp::Header header;
  header.seq = 0u;
  header.channel_id = 3U;
  header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x03}};
  static_cast<void>(
      handler.on_packet(make_packet_view(header, payload), 100U,
                        ControlKind::None, rx));

  EXPECT_EQ(rx.next_expected, 1U);
  EXPECT_EQ(rx.received_bits, 0ULL);

  const auto events = handler.drain_events(rx);
  EXPECT_TRUE(events.empty());
}

}  // namespace
