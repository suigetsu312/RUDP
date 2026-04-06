#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Rudp/Session.hpp"

namespace {

using Rudp::Session::ConnectionState;
using Rudp::Session::Session;
using Rudp::Session::SessionEvent;
using Rudp::Session::SessionRole;

[[nodiscard]] std::string_view to_string(ConnectionState state) {
  switch (state) {
    case ConnectionState::Closed:
      return "Closed";
    case ConnectionState::HandshakeSent:
      return "HandshakeSent";
    case ConnectionState::HandshakeReceived:
      return "HandshakeReceived";
    case ConnectionState::Established:
      return "Established";
    case ConnectionState::Closing:
      return "Closing";
    case ConnectionState::Reset:
      return "Reset";
  }
  return "UnknownState";
}

[[nodiscard]] std::string_view to_string(SessionEvent::Type type) {
  switch (type) {
    case SessionEvent::Type::DataReceived:
      return "DataReceived";
    case SessionEvent::Type::Connected:
      return "Connected";
    case SessionEvent::Type::ConnectionClosed:
      return "ConnectionClosed";
    case SessionEvent::Type::ConnectionReset:
      return "ConnectionReset";
    case SessionEvent::Type::Error:
      return "Error";
  }
  return "UnknownEvent";
}

void log_header(std::string_view label, const Rudp::Header& header) {
  std::cout << "[session-test] " << label
            << " seq=" << header.seq
            << " ack=" << header.ack
            << " channel_id=" << header.channel_id
            << " flags={"
            << (header.hasFlag(Rudp::Flag::Syn) ? " SYN" : "")
            << (header.hasFlag(Rudp::Flag::HandshakeAck) ? " ACK" : "")
            << (header.hasFlag(Rudp::Flag::Fin) ? " FIN" : "")
            << (header.hasFlag(Rudp::Flag::Rst) ? " RST" : "")
            << (header.hasFlag(Rudp::Flag::Ping) ? " PING" : "")
            << (header.hasFlag(Rudp::Flag::Pong) ? " PONG" : "")
            << " }\n";
}

void log_state(std::string_view label, const Session& session) {
  std::cout << "[session-test] " << label
            << " state=" << to_string(session.connection_state()) << '\n';
}

void log_events(std::string_view label,
                const std::vector<SessionEvent>& events) {
  std::cout << "[session-test] " << label
            << " events=" << events.size() << '\n';
  for (const auto& event : events) {
    std::cout << "  - type=" << to_string(event.type)
              << " channel_id=" << event.channel_id
              << " payload_size=" << event.payload.size();
    if (!event.error_message.empty()) {
      std::cout << " error=\"" << event.error_message << "\"";
    }
    std::cout << '\n';
  }
}

[[nodiscard]] Rudp::Header decode_header_or_die(
    const std::optional<std::vector<std::byte>>& datagram) {
  EXPECT_TRUE(datagram.has_value());
  const auto decoded = Rudp::Codec::decode(*datagram);
  EXPECT_TRUE(decoded.has_value());
  return decoded->header;
}

void establish_connection(Session& client, Session& server,
                          std::uint64_t base_time_ms = 100U) {
  const auto syn = client.poll_tx(base_time_ms);
  log_header("client -> server SYN", decode_header_or_die(syn));
  server.on_datagram_received(*syn, base_time_ms + 10U);
  log_state("server after SYN", server);

  const auto syn_ack = server.poll_tx(base_time_ms + 20U);
  log_header("server -> client SYN-ACK", decode_header_or_die(syn_ack));
  client.on_datagram_received(*syn_ack, base_time_ms + 30U);
  log_state("client after SYN-ACK", client);

  const auto final_ack = client.poll_tx(base_time_ms + 40U);
  log_header("client -> server final ACK", decode_header_or_die(final_ack));
  server.on_datagram_received(*final_ack, base_time_ms + 50U);
  log_state("server after final ACK", server);

  const auto trailing_control = server.poll_tx(base_time_ms + 60U);
  if (trailing_control.has_value()) {
    log_header("server post-handshake trailing control",
               decode_header_or_die(trailing_control));
  }
}

// Verifies queued unreliable application data is emitted as a datagram without
// consuming reliable sequence numbers.
TEST(SessionSkeletonTest, QueueSendPollTxProducesPacketForUnreliableData) {
  Session session(SessionRole::Server);

  const std::array payload = {std::byte{0x01}, std::byte{0x02}};
  session.queue_send(7U, Rudp::ChannelType::Unreliable, payload);

  const auto packet = session.poll_tx(100U);

  ASSERT_TRUE(packet.has_value());
  const auto decoded = Rudp::Codec::decode(*packet);
  ASSERT_TRUE(decoded.has_value());
  log_header("server queued unreliable packet", decoded->header);
  EXPECT_EQ(decoded->header.channel_id, 7U);
  EXPECT_EQ(decoded->header.channel_type, Rudp::ChannelType::Unreliable);
  EXPECT_EQ(decoded->header.seq, 0U);
  ASSERT_EQ(decoded->payload.size(), payload.size());
}

// Verifies receiving an unreliable datagram produces a single DataReceived
// event carrying the original channel metadata and payload.
TEST(SessionSkeletonTest, ReceiveUnreliableDatagramCreatesDataEvent) {
  Session session;

  Rudp::Header header;
  header.channel_id = 9U;
  header.channel_type = Rudp::ChannelType::Unreliable;

  const std::array payload = {std::byte{0xaa}, std::byte{0xbb}};
  const auto datagram = Rudp::Codec::encode(header, payload);

  session.on_datagram_received(datagram, 200U);
  const auto events = session.drain_events();
  log_header("received unreliable packet", header);
  log_events("events after unreliable receive", events);

  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type,
            Rudp::Session::SessionEvent::Type::DataReceived);
  EXPECT_EQ(events.front().channel_id, 9U);
  EXPECT_EQ(events.front().channel_type, Rudp::ChannelType::Unreliable);
  ASSERT_EQ(events.front().payload.size(), payload.size());
}

// Verifies the client starts connection establishment by sending SYN on the
// first transmit poll.
TEST(SessionSkeletonTest, ClientPollTxStartsHandshakeWithSyn) {
  Session client;

  const auto packet = client.poll_tx(100U);

  ASSERT_TRUE(packet.has_value());
  const auto decoded = Rudp::Codec::decode(*packet);
  ASSERT_TRUE(decoded.has_value());
  log_header("client initial poll_tx", decoded->header);
  log_state("client after initial poll_tx", client);
  EXPECT_TRUE(decoded->header.hasFlag(Rudp::Flag::Syn));
  EXPECT_FALSE(decoded->header.hasFlag(Rudp::Flag::HandshakeAck));
  EXPECT_EQ(client.connection_state(), ConnectionState::HandshakeSent);
}

// Verifies the full three-step handshake drives both peers into Established
// and emits Connected once on each side.
TEST(SessionSkeletonTest, HandshakeSucceedsAndBothPeersBecomeEstablished) {
  Session client;
  Session server(SessionRole::Server);

  establish_connection(client, server);
  log_state("client final", client);
  log_state("server final", server);

  EXPECT_EQ(client.connection_state(), ConnectionState::Established);
  EXPECT_EQ(server.connection_state(), ConnectionState::Established);

  const auto client_events = client.drain_events();
  const auto server_events = server.drain_events();
  log_events("client handshake events", client_events);
  log_events("server handshake events", server_events);
  ASSERT_EQ(client_events.size(), 1U);
  ASSERT_EQ(server_events.size(), 1U);
  EXPECT_EQ(client_events.front().type, SessionEvent::Type::Connected);
  EXPECT_EQ(server_events.front().type, SessionEvent::Type::Connected);
}

// Verifies a reset received during the handshake aborts the connection and
// emits ConnectionReset.
TEST(SessionSkeletonTest, HandshakeInterruptedByResetProducesConnectionReset) {
  Session client;

  const auto syn = decode_header_or_die(client.poll_tx(100U));
  log_header("client sent SYN before reset", syn);
  EXPECT_TRUE(syn.hasFlag(Rudp::Flag::Syn));
  EXPECT_EQ(client.connection_state(), ConnectionState::HandshakeSent);

  Rudp::Header rst_header;
  rst_header.flags = static_cast<Rudp::Flags>(Rudp::Flag::Rst);
  const auto rst = Rudp::Codec::encode(rst_header, std::array<std::byte, 0>{});

  client.on_datagram_received(rst, 110U);
  log_header("peer -> client RST", rst_header);
  log_state("client after RST during handshake", client);

  EXPECT_EQ(client.connection_state(), ConnectionState::Reset);
  const auto events = client.drain_events();
  log_events("client events after handshake reset", events);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::ConnectionReset);
}

// Verifies an established session can receive application data and then a
// reset, producing both the data event and the reset event in order.
TEST(SessionSkeletonTest,
     EstablishedConnectionReceivesDataThenResetProducesDataAndResetEvents) {
  Session client;
  Session server(SessionRole::Server);
  establish_connection(client, server);
  static_cast<void>(client.drain_events());
  static_cast<void>(server.drain_events());

  const std::array payload = {std::byte{0xde}, std::byte{0xad}};
  server.queue_send(42U, Rudp::ChannelType::Unreliable, payload);
  const auto data_packet = server.poll_tx(200U);
  const auto data_header = decode_header_or_die(data_packet);
  log_header("server -> client unreliable data", data_header);
  EXPECT_EQ(data_header.channel_id, 42U);
  EXPECT_EQ(data_header.channel_type, Rudp::ChannelType::Unreliable);
  client.on_datagram_received(*data_packet, 210U);

  Rudp::Header rst_header;
  rst_header.flags = static_cast<Rudp::Flags>(Rudp::Flag::Rst);
  const auto rst = Rudp::Codec::encode(rst_header, std::array<std::byte, 0>{});
  client.on_datagram_received(rst, 220U);
  log_header("peer -> client reset after data", rst_header);
  log_state("client after data then reset", client);

  EXPECT_EQ(client.connection_state(), ConnectionState::Reset);
  const auto events = client.drain_events();
  log_events("client events after data then reset", events);
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events[0].type, SessionEvent::Type::DataReceived);
  EXPECT_EQ(events[0].channel_id, 42U);
  EXPECT_EQ(events[0].channel_type, Rudp::ChannelType::Unreliable);
  ASSERT_EQ(events[0].payload.size(), payload.size());
  EXPECT_EQ(events[1].type, SessionEvent::Type::ConnectionReset);
}

// Verifies a peer can initiate graceful shutdown with FIN and the receiver
// moves into Closing while producing a ConnectionClosed event.
TEST(SessionSkeletonTest, EstablishedConnectionCanCloseGracefullyWithFin) {
  Session client;
  Session server(SessionRole::Server);
  establish_connection(client, server);
  static_cast<void>(client.drain_events());
  static_cast<void>(server.drain_events());

  server.request_close();
  log_state("server after request_close", server);
  EXPECT_EQ(server.connection_state(), ConnectionState::Closing);

  const auto fin_packet = server.poll_tx(300U);
  const auto fin = decode_header_or_die(fin_packet);
  log_header("server -> client FIN", fin);
  EXPECT_TRUE(fin.hasFlag(Rudp::Flag::Fin));
  EXPECT_FALSE(fin.hasFlag(Rudp::Flag::Syn));
  client.on_datagram_received(*fin_packet, 310U);
  log_state("client after receiving FIN", client);

  EXPECT_EQ(client.connection_state(), ConnectionState::Closing);
  const auto client_events = client.drain_events();
  log_events("client events after FIN", client_events);
  ASSERT_EQ(client_events.size(), 1U);
  EXPECT_EQ(client_events.front().type, SessionEvent::Type::ConnectionClosed);

  const auto ack = decode_header_or_die(client.poll_tx(320U));
  log_header("client -> server ACK after FIN", ack);
  EXPECT_FALSE(ack.hasFlag(Rudp::Flag::Fin));
  EXPECT_FALSE(ack.hasFlag(Rudp::Flag::Syn));
}

// Verifies invalid control-flag combinations do not mutate lifecycle state and
// are surfaced as an Error event.
TEST(SessionSkeletonTest, InvalidControlFlagCombinationDoesNotMutateState) {
  Session client;

  const auto syn = client.poll_tx(100U);
  ASSERT_TRUE(syn.has_value());
  log_header("client initial SYN before invalid flags", decode_header_or_die(syn));
  EXPECT_EQ(client.connection_state(), ConnectionState::HandshakeSent);

  Rudp::Header invalid_header;
  invalid_header.flags =
      Rudp::Flag::Syn | Rudp::Flag::Fin | Rudp::Flag::HandshakeAck;
  const auto invalid_datagram =
      Rudp::Codec::encode(invalid_header, std::array<std::byte, 0>{});

  client.on_datagram_received(invalid_datagram, 110U);
  log_header("peer -> client invalid control flags", invalid_header);
  log_state("client after invalid control flags", client);

  EXPECT_EQ(client.connection_state(), ConnectionState::HandshakeSent);
  const auto events = client.drain_events();
  log_events("client events after invalid control flags", events);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::Error);
}

// Verifies that when a future reliable packet arrives before the missing one,
// the ACK front advances across the already-received range once the gap is
// filled.
TEST(SessionSkeletonTest, ReliableAckWindowAdvancesAcrossPreviouslyReceivedGap) {
  Session receiver(SessionRole::Server);

  Rudp::Header future_header;
  future_header.seq = 11U;
  future_header.channel_id = 5U;
  future_header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x01}};
  receiver.on_datagram_received(Rudp::Codec::encode(future_header, payload), 100U);
  log_header("future reliable packet arrives first", future_header);
  EXPECT_EQ(receiver.next_expected_seq(), 12U);

  Rudp::Header gap_fill_header;
  gap_fill_header.seq = 12U;
  gap_fill_header.channel_id = 5U;
  gap_fill_header.channel_type = Rudp::ChannelType::ReliableUnordered;

  receiver.on_datagram_received(Rudp::Codec::encode(gap_fill_header, payload), 110U);
  log_header("gap-filling reliable packet arrives", gap_fill_header);
  EXPECT_EQ(receiver.next_expected_seq(), 13U);
}

// Verifies a stale reliable packet that falls behind the cumulative ACK front
// is dropped and not delivered to the application twice.
TEST(SessionSkeletonTest, StaleReliablePacketIsDroppedAfterAckFrontAdvances) {
  Session receiver(SessionRole::Server);

  Rudp::Header first_header;
  first_header.seq = 21U;
  first_header.channel_id = 8U;
  first_header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x33}};
  receiver.on_datagram_received(Rudp::Codec::encode(first_header, payload), 100U);
  auto events = receiver.drain_events();
  log_events("events after first reliable packet", events);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::DataReceived);

  receiver.on_datagram_received(Rudp::Codec::encode(first_header, payload), 110U);
  events = receiver.drain_events();
  log_events("events after stale duplicate reliable packet", events);
  EXPECT_TRUE(events.empty());
}

// Verifies the same out-of-order reliable packet is not delivered multiple
// times before the missing gap is filled.
TEST(SessionSkeletonTest, DuplicateFutureReliablePacketIsDropped) {
  Session receiver(SessionRole::Server);

  Rudp::Header future_header;
  future_header.seq = 31U;
  future_header.channel_id = 9U;
  future_header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x44}};
  receiver.on_datagram_received(Rudp::Codec::encode(future_header, payload), 100U);
  auto events = receiver.drain_events();
  log_events("events after first future reliable packet", events);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::DataReceived);

  receiver.on_datagram_received(Rudp::Codec::encode(future_header, payload), 110U);
  events = receiver.drain_events();
  log_events("events after duplicate future reliable packet", events);
  EXPECT_TRUE(events.empty());
}

// Verifies a reliable packet that is too far ahead of the current ACK window
// is dropped instead of being delivered without ACK-window representation.
TEST(SessionSkeletonTest, FutureReliablePacketOutsideAckWindowIsDropped) {
  Session receiver(SessionRole::Server);

  Rudp::Header first_header;
  first_header.seq = 100U;
  first_header.channel_id = 10U;
  first_header.channel_type = Rudp::ChannelType::ReliableUnordered;

  const std::array payload = {std::byte{0x55}};
  receiver.on_datagram_received(Rudp::Codec::encode(first_header, payload),
                                100U);
  auto events = receiver.drain_events();
  log_events("events after initial reliable packet", events);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::DataReceived);

  Rudp::Header far_future_header;
  far_future_header.seq =
      first_header.seq +
      static_cast<std::uint32_t>(Rudp::kAckBitsWindow) + 2U;
  far_future_header.channel_id = 10U;
  far_future_header.channel_type = Rudp::ChannelType::ReliableUnordered;

  receiver.on_datagram_received(
      Rudp::Codec::encode(far_future_header, payload), 110U);
  events = receiver.drain_events();
  log_events("events after far-future reliable packet", events);
  EXPECT_TRUE(events.empty());
}

}  // namespace
