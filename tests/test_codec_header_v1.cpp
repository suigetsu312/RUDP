#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include "Rudp/Codec.hpp"

namespace {

// Verifies a full encode/decode round-trip preserves every header field and
// the payload bytes.
TEST(CodecHeaderTest, EncodeDecodeRoundTripPreservesHeaderAndPayload) {
  Rudp::Header header;
  header.conn_id = 100u;
  header.seq = 200u;
  header.ack = 300u;
  header.ack_bits = 0x55aa55aa55aa55aaULL;
  header.channel_id = 400u;
  header.channel_type = Rudp::ChannelType::ReliableOrdered;
  header.flags = Rudp::Flag::Syn | Rudp::Flag::Ack;

  const std::array payload = {
      std::byte{0xde},
      std::byte{0xad},
      std::byte{0xbe},
      std::byte{0xef},
  };

  const auto bytes = Rudp::Codec::encode(header, payload);

  const auto decoded = Rudp::Codec::decode(bytes);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->header.conn_id, header.conn_id);
  EXPECT_EQ(decoded->header.seq, header.seq);
  EXPECT_EQ(decoded->header.ack, header.ack);
  EXPECT_EQ(decoded->header.ack_bits, header.ack_bits);
  EXPECT_EQ(decoded->header.channel_id, header.channel_id);
  EXPECT_EQ(decoded->header.channel_type, header.channel_type);
  EXPECT_EQ(decoded->header.flags, header.flags);
  ASSERT_EQ(decoded->payload.size(), payload.size());
  EXPECT_EQ(decoded->payload[0], payload[0]);
  EXPECT_EQ(decoded->payload[1], payload[1]);
  EXPECT_EQ(decoded->payload[2], payload[2]);
  EXPECT_EQ(decoded->payload[3], payload[3]);
}

// Verifies decode rejects buffers that are shorter than the fixed header size.
TEST(CodecHeaderTest, DecodeRejectsShortBuffer) {
  const std::array bytes = {
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
  };

  const auto decoded = Rudp::Codec::decode(bytes);

  EXPECT_FALSE(decoded.has_value());
}

// Verifies header validation rejects packets with the wrong fixed header size.
TEST(CodecHeaderTest, HeaderValidationRejectsWrongHeaderLength) {
  Rudp::Header header;
  header.header_len = 27;

  EXPECT_FALSE(Rudp::Codec::isValidHeader(header));
}

// Verifies header validation rejects non-zero reserved bytes.
TEST(CodecHeaderTest, HeaderValidationRejectsReservedByte) {
  Rudp::Header header;
  header.reserved = 1;

  EXPECT_FALSE(Rudp::Codec::isValidHeader(header));
}

// Verifies header validation rejects use of reserved flag bits.
TEST(CodecHeaderTest, HeaderValidationRejectsReservedFlagBits) {
  Rudp::Header header;
  header.flags = 0x80;

  EXPECT_FALSE(Rudp::Codec::isValidHeader(header));
}

// Verifies decode rejects packets whose channel type is outside the supported
// enum range.
TEST(CodecHeaderTest, DecodeRejectsInvalidChannelType) {
  auto bytes = Rudp::Codec::encode(Rudp::Header{}, {});
  bytes[24] = std::byte{0x7f};

  const auto decoded = Rudp::Codec::decode(bytes);

  EXPECT_FALSE(decoded.has_value());
}

// Verifies Header::hasFlag reports set and unset bits correctly.
TEST(ProtocolHeaderTest, HasFlagChecksBitPresence) {
  Rudp::Header header;
  header.flags = Rudp::Flag::Syn | Rudp::Flag::Ping;

  EXPECT_TRUE(header.hasFlag(Rudp::Flag::Syn));
  EXPECT_TRUE(header.hasFlag(Rudp::Flag::Ping));
  EXPECT_FALSE(header.hasFlag(Rudp::Flag::Fin));
}

}  // namespace
