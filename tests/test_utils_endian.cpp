#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "Rudp/Codec.hpp"
#include "Rudp/Utils.hpp"

namespace {

// Verifies that codec serialization writes fixed-width integer header fields
// in big-endian byte order.
TEST(CodecEndianTest, EncodesU32AndU64FieldsAsBigEndian) {
  Rudp::Header header;
  header.conn_id = 0x01020304u;
  header.seq = 0x11121314u;
  header.ack = 0x21222324u;
  header.ack_bits = 0x3132333435363738ULL;
  header.channel_id = 0x41424344u;
  header.channel_type = Rudp::ChannelType::ReliableUnordered;
  header.flags = static_cast<Rudp::Flags>(Rudp::Flag::Ping);

  const auto bytes = Rudp::Codec::encode(header, {});

  ASSERT_EQ(bytes.size(), Rudp::kHeaderLength);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[0]), 0x01);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[1]), 0x02);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[2]), 0x03);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[3]), 0x04);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[12]), 0x31);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[13]), 0x32);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[18]), 0x37);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[19]), 0x38);
}

// Verifies that 32-bit utility helpers preserve values across write/read.
TEST(UtilsEndianTest, ReadAndWriteU32RoundTrip) {
  std::array<std::byte, 4> bytes{};

  Rudp::Utils::writeU32(bytes, 0, 0x01020304u);

  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[0]), 0x01);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[1]), 0x02);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[2]), 0x03);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[3]), 0x04);
  EXPECT_EQ(Rudp::Utils::readU32(bytes, 0), 0x01020304u);
}

// Verifies that 64-bit utility helpers preserve values across write/read.
TEST(UtilsEndianTest, ReadAndWriteU64RoundTrip) {
  std::array<std::byte, 8> bytes{};

  Rudp::Utils::writeU64(bytes, 0, 0x0102030405060708ULL);

  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[0]), 0x01);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[1]), 0x02);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[6]), 0x07);
  EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[7]), 0x08);
  EXPECT_EQ(Rudp::Utils::readU64(bytes, 0), 0x0102030405060708ULL);
}

// Verifies sequence comparison helpers behave correctly around wrap-around.
TEST(ProtocolSeqTest, SequenceComparisonHandlesWrapAround) {
  EXPECT_TRUE(Rudp::seq_lt(0u, 1u));
  EXPECT_TRUE(Rudp::seq_lt(0xffffffffu, 0u));
  EXPECT_TRUE(Rudp::seq_gt(0u, 0xffffffffu));
  EXPECT_TRUE(Rudp::seq_le(7u, 7u));
  EXPECT_TRUE(Rudp::seq_ge(7u, 7u));
}

}  // namespace
