#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "Rudp/SessionTypes.hpp"
#include "Rudp/TxHandler.hpp"

namespace {

using Rudp::Session::OwnedPacket;
using Rudp::Session::TxEntry;
using Rudp::Session::TxHandler;
using Rudp::Session::ConnectionState;
using Rudp::Session::RxSessionState;
using Rudp::Session::SessionRole;
using Rudp::Session::TxSessionState;

void seed_inflight(TxSessionState& tx,
                   std::initializer_list<std::uint32_t> seqs) {
  for (const auto seq : seqs) {
    TxEntry entry;
    entry.packet = OwnedPacket{
        .header =
            Rudp::Header{
                .seq = seq,
                .channel_id = 1U,
                .channel_type = Rudp::ChannelType::ReliableUnordered,
            },
        .payload = std::vector<std::byte>{std::byte{0x01}},
    };
    tx.inflight.emplace(seq, std::move(entry));
  }
}

// Verifies on_remote_ack marks every missing inflight packet between the ACK
// front and the furthest selectively acknowledged future packet.
TEST(TxHandlerAckTest, AckBitsGapDetectionMarksMultipleMissingPackets) {
  TxHandler handler;
  TxSessionState tx;
  seed_inflight(tx, {100U, 101U, 102U, 103U, 104U, 105U, 106U, 107U});

  const std::uint32_t ack = 100U;
  const std::uint64_t ack_bits =
      (1ULL << 1U) | (1ULL << 2U) | (1ULL << 3U) | (1ULL << 5U) |
      (1ULL << 6U);

  static_cast<void>(handler.on_remote_ack(ack, ack_bits, tx));

  EXPECT_EQ(tx.remote_ack, ack);
  EXPECT_EQ(tx.remote_ack_bits, ack_bits);

  ASSERT_NE(tx.inflight.find(100U), tx.inflight.end());
  ASSERT_NE(tx.inflight.find(101U), tx.inflight.end());
  ASSERT_NE(tx.inflight.find(105U), tx.inflight.end());
  EXPECT_TRUE(tx.inflight.at(100U).fast_retx_pending);
  EXPECT_TRUE(tx.inflight.at(101U).fast_retx_pending);
  EXPECT_TRUE(tx.inflight.at(105U).fast_retx_pending);

  EXPECT_EQ(tx.inflight.find(102U), tx.inflight.end());
  EXPECT_EQ(tx.inflight.find(103U), tx.inflight.end());
  EXPECT_EQ(tx.inflight.find(104U), tx.inflight.end());
  EXPECT_EQ(tx.inflight.find(106U), tx.inflight.end());
  EXPECT_EQ(tx.inflight.find(107U), tx.inflight.end());
}

// Verifies no fast retransmit candidates are marked when the peer reports only
// a plain cumulative ACK with no selective future receptions.
TEST(TxHandlerAckTest, ZeroAckBitsDoesNotMarkFastRetransmitCandidates) {
  TxHandler handler;
  TxSessionState tx;
  seed_inflight(tx, {100U, 101U, 102U});

  static_cast<void>(handler.on_remote_ack(100U, 0ULL, tx));

  ASSERT_NE(tx.inflight.find(100U), tx.inflight.end());
  ASSERT_NE(tx.inflight.find(101U), tx.inflight.end());
  ASSERT_NE(tx.inflight.find(102U), tx.inflight.end());
  EXPECT_FALSE(tx.inflight.at(100U).fast_retx_pending);
  EXPECT_FALSE(tx.inflight.at(101U).fast_retx_pending);
  EXPECT_FALSE(tx.inflight.at(102U).fast_retx_pending);
}

// Verifies retransmission attempts stop after the fixed retry-count cap instead
// of retrying forever.
TEST(TxHandlerAckTest, RetransmissionStopsAfterFixedRetryLimit) {
  TxHandler handler;
  TxSessionState tx;
  RxSessionState rx;
  ConnectionState connection_state = ConnectionState::Established;
  seed_inflight(tx, {200U});

  const std::array retry_times = {
      250ULL,
      750ULL,
      1750ULL,
      3750ULL,
      7750ULL,
      11750ULL,
  };

  for (std::size_t i = 0; i < retry_times.size() - 1U; ++i) {
    const auto result =
        handler.poll(retry_times[i], SessionRole::Server, 1234U,
                     connection_state, rx, tx);
    ASSERT_FALSE(result.fatal_error);
    ASSERT_TRUE(result.datagram.has_value());
    ASSERT_NE(tx.inflight.find(200U), tx.inflight.end());
    EXPECT_EQ(tx.inflight.at(200U).retry_count, i + 1U);
  }

  const auto exhausted_result =
      handler.poll(retry_times.back(), SessionRole::Server, 1234U,
                   connection_state, rx, tx);
  EXPECT_TRUE(exhausted_result.fatal_error);
  EXPECT_FALSE(exhausted_result.datagram.has_value());
  EXPECT_EQ(exhausted_result.error_message,
            "retransmission retry limit exceeded");
  EXPECT_EQ(tx.inflight.find(200U), tx.inflight.end());
}

TEST(TxHandlerAckTest, FinalHandshakeAckDoesNotEnterRetransmitInflight) {
  TxHandler handler;
  TxSessionState tx;
  RxSessionState rx;
  ConnectionState connection_state = ConnectionState::Established;
  tx.final_ack_pending = true;

  const auto first_result =
      handler.poll(0U, SessionRole::Client, 1234U, connection_state, rx, tx);
  ASSERT_FALSE(first_result.fatal_error);
  ASSERT_TRUE(first_result.datagram.has_value());
  EXPECT_TRUE(tx.inflight.empty());

  const auto later_result =
      handler.poll(20'000U, SessionRole::Client, 1234U, connection_state, rx,
                   tx);
  EXPECT_FALSE(later_result.fatal_error);
  EXPECT_FALSE(later_result.datagram.has_value());
}

}  // namespace
