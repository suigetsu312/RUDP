#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "Rudp/Codec.hpp"
#include "Rudp/ServerSessionManager.hpp"

namespace {

[[nodiscard]] std::vector<std::byte> encode_control_datagram(
    Rudp::Flags flags,
    std::uint32_t conn_id = 0,
    std::uint32_t seq = 1,
    std::uint32_t ack = 0,
    std::uint64_t ack_bits = 0) {
  const Rudp::Header header{
      .conn_id = conn_id,
      .seq = seq,
      .ack = ack,
      .ack_bits = ack_bits,
      .channel_id = 0,
      .channel_type = Rudp::ChannelType::Unreliable,
      .flags = flags,
      .header_len = Rudp::kHeaderLength,
      .reserved = 0,
  };

  return Rudp::Codec::encode(header, {});
}

}  // namespace

namespace Rudp::Session {

TEST(ServerSessionManagerTest, ZeroConnIdSynCreatesPendingServerSession) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"192.168.1.50", 40000};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 77);

  manager.on_datagram_received(endpoint, syn, 100U);

  ASSERT_EQ(manager.pending_session_count(), 1U);
  EXPECT_EQ(manager.active_session_count(), 0U);
  EXPECT_TRUE(manager.has_pending_session(endpoint));

  const auto conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(conn_id.has_value());
  EXPECT_NE(*conn_id, 0U);

  const auto state = manager.pending_connection_state(endpoint);
  ASSERT_TRUE(state.has_value());
  EXPECT_EQ(*state, ConnectionState::HandshakeReceived);
}

TEST(ServerSessionManagerTest,
     PendingSessionPromotesToActiveAfterFinalHandshakeAck) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"192.168.1.60", 40010};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 77);
  manager.on_datagram_received(endpoint, syn, 100U);

  const auto assigned_conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(assigned_conn_id.has_value());
  ASSERT_NE(*assigned_conn_id, 0U);

  const auto final_ack = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Ack), *assigned_conn_id, 78);
  manager.on_datagram_received(endpoint, final_ack, 200U);

  EXPECT_EQ(manager.pending_session_count(), 0U);
  EXPECT_EQ(manager.active_session_count(), 1U);
  EXPECT_FALSE(manager.has_pending_session(endpoint));
  EXPECT_EQ(manager.active_conn_id(endpoint), assigned_conn_id);
  EXPECT_TRUE(manager.has_active_session(*assigned_conn_id));
}

TEST(ServerSessionManagerTest, DuplicateSynReusesExistingPendingSession) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"192.168.1.50", 40000};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 88);

  manager.on_datagram_received(endpoint, syn, 100U);
  const auto first_conn_id = manager.pending_conn_id(endpoint);

  manager.on_datagram_received(endpoint, syn, 200U);

  ASSERT_EQ(manager.pending_session_count(), 1U);
  ASSERT_TRUE(first_conn_id.has_value());
  EXPECT_EQ(manager.pending_conn_id(endpoint), first_conn_id);
}

TEST(ServerSessionManagerTest, ActiveConnIdDispatchRoutesPacketToActiveSession) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"192.168.1.70", 40020};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 77);
  manager.on_datagram_received(endpoint, syn, 100U);

  const auto assigned_conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(assigned_conn_id.has_value());

  const auto final_ack = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Ack), *assigned_conn_id, 78);
  manager.on_datagram_received(endpoint, final_ack, 200U);

  ASSERT_TRUE(manager.has_active_session(*assigned_conn_id));
  static_cast<void>(manager.drain_active_events(*assigned_conn_id));

  const Rudp::Header header{
      .conn_id = *assigned_conn_id,
      .seq = 0,
      .ack = 0,
      .ack_bits = 0,
      .channel_id = 7,
      .channel_type = Rudp::ChannelType::Unreliable,
      .flags = 0,
      .header_len = Rudp::kHeaderLength,
      .reserved = 0,
  };
  const std::vector payload = {std::byte{0x11}, std::byte{0x22}};
  const auto datagram = Rudp::Codec::encode(header, payload);

  manager.on_datagram_received(endpoint, datagram, 300U);

  const auto events = manager.drain_active_events(*assigned_conn_id);
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, SessionEvent::Type::DataReceived);
  EXPECT_EQ(events.front().channel_id, 7U);
  ASSERT_EQ(events.front().payload.size(), payload.size());
  EXPECT_EQ(events.front().payload[0], payload[0]);
  EXPECT_EQ(events.front().payload[1], payload[1]);
}

TEST(ServerSessionManagerTest, ActiveConnIdFromDifferentEndpointIsDropped) {
  ServerSessionManager manager;
  const EndpointKey bound_endpoint{"192.168.1.71", 40021};
  const EndpointKey mismatched_endpoint{"192.168.1.72", 40022};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 77);
  manager.on_datagram_received(bound_endpoint, syn, 100U);

  const auto assigned_conn_id = manager.pending_conn_id(bound_endpoint);
  ASSERT_TRUE(assigned_conn_id.has_value());

  const auto final_ack = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Ack), *assigned_conn_id, 78);
  manager.on_datagram_received(bound_endpoint, final_ack, 200U);
  static_cast<void>(manager.drain_active_events(*assigned_conn_id));

  const Rudp::Header header{
      .conn_id = *assigned_conn_id,
      .seq = 0,
      .ack = 0,
      .ack_bits = 0,
      .channel_id = 7,
      .channel_type = Rudp::ChannelType::Unreliable,
      .flags = 0,
      .header_len = Rudp::kHeaderLength,
      .reserved = 0,
  };
  const std::vector payload = {std::byte{0x33}};
  const auto datagram = Rudp::Codec::encode(header, payload);

  manager.on_datagram_received(mismatched_endpoint, datagram, 300U);

  const auto events = manager.drain_active_events(*assigned_conn_id);
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(manager.pending_session_count(), 0U);
  EXPECT_EQ(manager.active_session_count(), 1U);
}

TEST(ServerSessionManagerTest, UnknownPeerCreatesPendingSessionBeforeHandshake) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"192.168.1.99", 41000};

  const auto fin =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Fin), 0, 12);

  manager.on_datagram_received(endpoint, fin, 100U);

  EXPECT_EQ(manager.pending_session_count(), 1U);
  EXPECT_TRUE(manager.has_pending_session(endpoint));
  const auto conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(conn_id.has_value());
  EXPECT_NE(*conn_id, 0U);
}

TEST(ServerSessionManagerTest,
     UnknownPeerUnreliableDoesNotLeaveClosedPendingSessionBehind) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"10.0.0.7", 42000};

  const Rudp::Header header{
      .conn_id = 0,
      .seq = 0,
      .ack = 0,
      .ack_bits = 0,
      .channel_id = 42,
      .channel_type = Rudp::ChannelType::Unreliable,
      .flags = 0,
      .header_len = Rudp::kHeaderLength,
      .reserved = 0,
  };
  const std::vector payload = {std::byte{0xaa}, std::byte{0xbb}};
  const auto datagram = Rudp::Codec::encode(header, payload);

  manager.on_datagram_received(endpoint, datagram, 100U);

  EXPECT_EQ(manager.pending_session_count(), 0U);
  EXPECT_FALSE(manager.has_pending_session(endpoint));
  EXPECT_EQ(manager.pending_conn_id(endpoint), std::nullopt);
  EXPECT_EQ(manager.pending_connection_state(endpoint), std::nullopt);
}

TEST(ServerSessionManagerTest, UnknownNonZeroConnIdPacketIsDropped) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"172.16.0.8", 43000};

  const Rudp::Header header{
      .conn_id = 0x12345678U,
      .seq = 0,
      .ack = 0,
      .ack_bits = 0,
      .channel_id = 9,
      .channel_type = Rudp::ChannelType::Unreliable,
      .flags = 0,
      .header_len = Rudp::kHeaderLength,
      .reserved = 0,
  };
  const std::vector payload = {std::byte{0x44}};
  const auto datagram = Rudp::Codec::encode(header, payload);

  manager.on_datagram_received(endpoint, datagram, 100U);

  EXPECT_EQ(manager.pending_session_count(), 0U);
  EXPECT_EQ(manager.active_session_count(), 0U);
  EXPECT_FALSE(manager.has_pending_session(endpoint));
  EXPECT_EQ(manager.active_conn_id(endpoint), std::nullopt);
}

TEST(ServerSessionManagerTest, PendingSessionResetIsCleanedUpImmediately) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"172.16.0.9", 43001};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 90);
  manager.on_datagram_received(endpoint, syn, 100U);

  const auto assigned_conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(assigned_conn_id.has_value());

  const auto rst = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Rst), *assigned_conn_id, 0);
  manager.on_datagram_received(endpoint, rst, 110U);

  EXPECT_EQ(manager.pending_session_count(), 0U);
  EXPECT_FALSE(manager.has_pending_session(endpoint));
}

TEST(ServerSessionManagerTest, ActiveSessionResetIsCleanedUpImmediately) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"172.16.0.10", 43002};

  const auto syn =
      encode_control_datagram(static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 91);
  manager.on_datagram_received(endpoint, syn, 100U);

  const auto assigned_conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(assigned_conn_id.has_value());

  const auto final_ack = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Ack), *assigned_conn_id, 92);
  manager.on_datagram_received(endpoint, final_ack, 110U);
  ASSERT_TRUE(manager.has_active_session(*assigned_conn_id));

  const auto rst = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Rst), *assigned_conn_id, 0);
  manager.on_datagram_received(endpoint, rst, 120U);

  EXPECT_EQ(manager.active_session_count(), 0U);
  EXPECT_FALSE(manager.has_active_session(*assigned_conn_id));
  EXPECT_EQ(manager.active_conn_id(endpoint), std::nullopt);
}

TEST(ServerSessionManagerTest, RetiredConnIdsAreNotReused) {
  ServerSessionManager manager;
  std::vector<std::uint32_t> assigned_conn_ids;

  for (std::uint16_t i = 0; i < 8; ++i) {
    const EndpointKey endpoint{"172.16.1.1",
                               static_cast<std::uint16_t>(44010 + i)};
    const auto syn = encode_control_datagram(
        static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 100U + i);
    manager.on_datagram_received(endpoint, syn, 100U + i);

    const auto conn_id = manager.pending_conn_id(endpoint);
    ASSERT_TRUE(conn_id.has_value());
    assigned_conn_ids.push_back(*conn_id);

    const auto rst = encode_control_datagram(
        static_cast<Rudp::Flags>(Rudp::Flag::Rst), *conn_id, 0);
    manager.on_datagram_received(endpoint, rst, 200U + i);
    EXPECT_FALSE(manager.has_pending_session(endpoint));
  }

  std::sort(assigned_conn_ids.begin(), assigned_conn_ids.end());
  const auto duplicate_it =
      std::adjacent_find(assigned_conn_ids.begin(), assigned_conn_ids.end());
  EXPECT_EQ(duplicate_it, assigned_conn_ids.end());
}

TEST(ServerSessionManagerTest, PollTxCollectsAtMostOneDatagramPerSession) {
  ServerSessionManager manager;
  const EndpointKey pending_endpoint{"192.168.2.10", 44000};
  const EndpointKey active_endpoint{"192.168.2.11", 44001};

  const auto pending_syn = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 100);
  manager.on_datagram_received(pending_endpoint, pending_syn, 100U);

  const auto active_syn = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 200);
  manager.on_datagram_received(active_endpoint, active_syn, 110U);
  const auto active_conn_id = manager.pending_conn_id(active_endpoint);
  ASSERT_TRUE(active_conn_id.has_value());

  const auto final_ack = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Ack), *active_conn_id, 201);
  manager.on_datagram_received(active_endpoint, final_ack, 120U);

  const Rudp::Header active_reliable_header{
      .conn_id = *active_conn_id,
      .seq = 201,
      .ack = 0,
      .ack_bits = 0,
      .channel_id = 5,
      .channel_type = Rudp::ChannelType::ReliableUnordered,
      .flags = 0,
      .header_len = Rudp::kHeaderLength,
      .reserved = 0,
  };
  const auto active_reliable_datagram =
      Rudp::Codec::encode(active_reliable_header, {});
  manager.on_datagram_received(active_endpoint, active_reliable_datagram, 125U);

  ASSERT_EQ(manager.pending_session_count(), 1U);
  ASSERT_EQ(manager.active_session_count(), 1U);

  auto outbound = manager.poll_tx(130U);
  ASSERT_EQ(outbound.size(), 2U);

  bool saw_pending = false;
  bool saw_active = false;
  for (const auto& datagram : outbound) {
    const auto decoded = Rudp::Codec::decode(datagram.bytes);
    ASSERT_TRUE(decoded.has_value());

    if (datagram.endpoint == pending_endpoint) {
      saw_pending = true;
      EXPECT_TRUE(decoded->header.hasFlag(Rudp::Flag::Syn));
      EXPECT_TRUE(decoded->header.hasFlag(Rudp::Flag::Ack));
    }

    if (datagram.endpoint == active_endpoint) {
      saw_active = true;
      EXPECT_GE(decoded->header.ack, 201U);
    }
  }

  EXPECT_TRUE(saw_pending);
  EXPECT_TRUE(saw_active);
}

TEST(ServerSessionManagerTest, QueueSendTargetsActiveSession) {
  ServerSessionManager manager;
  const EndpointKey endpoint{"192.168.2.12", 44002};

  const auto syn = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Syn), 0, 300);
  manager.on_datagram_received(endpoint, syn, 100U);

  const auto conn_id = manager.pending_conn_id(endpoint);
  ASSERT_TRUE(conn_id.has_value());

  const auto final_ack = encode_control_datagram(
      static_cast<Rudp::Flags>(Rudp::Flag::Ack), *conn_id, 301);
  manager.on_datagram_received(endpoint, final_ack, 110U);
  ASSERT_TRUE(manager.has_active_session(*conn_id));

  const std::string message = "server->client";
  const auto* bytes =
      reinterpret_cast<const std::byte*>(message.data());
  EXPECT_TRUE(manager.queue_send(*conn_id, 1U, Rudp::ChannelType::Unreliable,
                                 std::span<const std::byte>(bytes,
                                                            message.size())));

  auto outbound = manager.poll_tx(120U);
  ASSERT_EQ(outbound.size(), 1U);
  EXPECT_EQ(outbound.front().endpoint, endpoint);

  const auto decoded = Rudp::Codec::decode(outbound.front().bytes);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->header.conn_id, *conn_id);
  EXPECT_EQ(decoded->header.channel_id, 1U);
  EXPECT_EQ(decoded->header.channel_type, Rudp::ChannelType::Unreliable);
  ASSERT_EQ(decoded->payload.size(), message.size());
}

}  // namespace Rudp::Session
