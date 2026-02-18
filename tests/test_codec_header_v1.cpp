#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <array>

#include "Rudp/Protocol.hpp"
#include "Rudp/Codec.hpp"

using namespace Rudp;

TEST(CodecHeaderV1, EncodeDecodeRoundTrip) {
    Protocol::HeaderV1 h{};
    h.ConnId = 0xAABBCCDDu;
    h.Seq = 123456u;
    h.Ack = 120000u;
    h.AckBits = 0xFEDCBA9876543210ull;
    h.TimestampMs = 987654321u;
    h.ChannelId = 7;
    h.PayloadLen = 5;

    std::vector<uint8_t> pkt(Protocol::HeaderV1Length + h.PayloadLen);
    ASSERT_TRUE(Codec::EncodeHeaderV1(h, pkt));

    auto decoded = Codec::DecodeHeaderV1(pkt);
    ASSERT_TRUE(decoded.has_value());

    const auto& d = decoded.value();
    EXPECT_EQ(d.ConnId, h.ConnId);
    EXPECT_EQ(d.Seq, h.Seq);
    EXPECT_EQ(d.Ack, h.Ack);
    EXPECT_EQ(d.AckBits, h.AckBits);
    EXPECT_EQ(d.TimestampMs, h.TimestampMs);
    EXPECT_EQ(d.ChannelId, h.ChannelId);
    EXPECT_EQ(d.PayloadLen, h.PayloadLen);
}

TEST(CodecHeaderV1, RejectsTooShortDatagram) {
    std::vector<uint8_t> pkt(Protocol::HeaderV1Length - 1);
    auto decoded = Codec::DecodeHeaderV1(pkt);
    EXPECT_FALSE(decoded.has_value());
}

TEST(CodecHeaderV1, RejectsTruncatedPayload) {
    Protocol::HeaderV1 h{};
    h.ConnId = 1;
    h.Seq = 2;
    h.Ack = 0;
    h.AckBits = 0;
    h.TimestampMs = 0;
    h.ChannelId = 1;
    h.PayloadLen = 10; // claims 10 bytes payload

    std::vector<uint8_t> pkt(Protocol::HeaderV1Length + 5); // truncated
    ASSERT_TRUE(Codec::EncodeHeaderV1(h, pkt));

    auto decoded = Codec::DecodeHeaderV1(pkt);
    EXPECT_FALSE(decoded.has_value());
}

TEST(CodecHeaderV1, EncodeFailsIfOutBufferTooSmall) {
    Protocol::HeaderV1 h{};
    h.ConnId = 1;
    h.Seq = 1;
    h.Ack = 0;
    h.AckBits = 0;
    h.TimestampMs = 0;
    h.ChannelId = 1;
    h.PayloadLen = 0;

    std::vector<uint8_t> out(Protocol::HeaderV1Length - 1);
    EXPECT_FALSE(Codec::EncodeHeaderV1(h, out));
}

// Strong endian regression: decode known big-endian bytes
TEST(CodecHeaderV1, DecodeKnownBigEndianBytes) {
    std::array<uint8_t, Protocol::HeaderV1Length> b{};

    b[0]=0x11; b[1]=0x22; b[2]=0x33; b[3]=0x44;          // ConnId
    b[4]=0x01; b[5]=0x02; b[6]=0x03; b[7]=0x04;          // Seq
    b[8]=0x0A; b[9]=0x0B; b[10]=0x0C; b[11]=0x0D;        // Ack
    b[12]=0x01; b[13]=0x02; b[14]=0x03; b[15]=0x04;      // AckBits
    b[16]=0x05; b[17]=0x06; b[18]=0x07; b[19]=0x08;
    b[20]=0xA1; b[21]=0xA2; b[22]=0xA3; b[23]=0xA4;      // TimestampMs
    b[24]=0x07; b[25]=0x00;                               // ChannelId, Reserved
    b[26]=0x00; b[27]=0x00;                               // PayloadLen = 16

    auto h = Codec::DecodeHeaderV1(b);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(h->ConnId, 0x11223344u);
    EXPECT_EQ(h->Seq, 0x01020304u);
    EXPECT_EQ(h->Ack, 0x0A0B0C0Du);
    EXPECT_EQ(h->AckBits, 0x0102030405060708ull);
    EXPECT_EQ(h->TimestampMs, 0xA1A2A3A4u);
    EXPECT_EQ(h->ChannelId, 7);
    EXPECT_EQ(h->PayloadLen, 0);
}
