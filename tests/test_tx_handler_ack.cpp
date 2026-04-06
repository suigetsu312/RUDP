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

  handler.on_remote_ack(ack, ack_bits, tx);

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

  handler.on_remote_ack(100U, 0ULL, tx);

  ASSERT_NE(tx.inflight.find(100U), tx.inflight.end());
  ASSERT_NE(tx.inflight.find(101U), tx.inflight.end());
  ASSERT_NE(tx.inflight.find(102U), tx.inflight.end());
  EXPECT_FALSE(tx.inflight.at(100U).fast_retx_pending);
  EXPECT_FALSE(tx.inflight.at(101U).fast_retx_pending);
  EXPECT_FALSE(tx.inflight.at(102U).fast_retx_pending);
}

}  // namespace
