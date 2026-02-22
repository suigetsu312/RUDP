#include <gtest/gtest.h>
#include <array>
#include <vector>

#include "Rudp/Protocol.hpp"
#include "Rudp/Codec.hpp"

using namespace Rudp::Protocol;
using namespace Rudp::Codec;

namespace {

constexpr std::size_t kBufSize = 1500;

HeaderV1 MakeBaseReliableHeader() {
    HeaderV1 h{};
    h.ConnId    = 0x11223344;
    h.Seq       = 100;
    h.Ack       = 50;
    h.AckBits   = 0xAABBCCDDEEFF0011ull;
    h.ChannelId = 42;
    h.ChType    = ChannelType::ReliableOrdered;
    h.Flags     = 0;
    h.HeaderLen = HeaderLenV1;
    h.Reserved  = 0;
    return h;
}

} // namespace

TEST(CodecHeaderV1, EncodeDecodeRoundTrip_Reliable)
{
    std::array<uint8_t, kBufSize> buf{};
    auto h = MakeBaseReliableHeader();

    ASSERT_TRUE(EncodeHeaderV1(h, buf));

    auto decoded = DecodeHeaderV1(buf);
    ASSERT_TRUE(decoded.has_value());

    EXPECT_EQ(decoded->ConnId, h.ConnId);
    EXPECT_EQ(decoded->Seq, h.Seq);
    EXPECT_EQ(decoded->Ack, h.Ack);
    EXPECT_EQ(decoded->AckBits, h.AckBits);
    EXPECT_EQ(decoded->ChannelId, h.ChannelId);
    EXPECT_EQ(decoded->ChType, h.ChType);
    EXPECT_EQ(decoded->Flags, h.Flags);
}

TEST(CodecHeaderV1, NonReliableMustHaveSeqZero)
{
    std::array<uint8_t, kBufSize> buf{};

    auto h = MakeBaseReliableHeader();
    h.ChType = ChannelType::Unreliable;
    h.Seq = 123; // invalid per spec

    EXPECT_FALSE(EncodeHeaderV1(h, buf));
}

TEST(CodecHeaderV1, ReliableAllowsNonZeroSeq)
{
    std::array<uint8_t, kBufSize> buf{};

    auto h = MakeBaseReliableHeader();
    h.Seq = 999;

    EXPECT_TRUE(EncodeHeaderV1(h, buf));
}

TEST(CodecHeaderV1, HeaderLenMustBe28)
{
    std::array<uint8_t, kBufSize> buf{};

    auto h = MakeBaseReliableHeader();
    ASSERT_TRUE(EncodeHeaderV1(h, buf));

    buf[OffHeaderLen] = 27; // corrupt

    auto decoded = DecodeHeaderV1(buf);
    EXPECT_FALSE(decoded.has_value());
}

TEST(CodecHeaderV1, ReservedMustBeZero)
{
    std::array<uint8_t, kBufSize> buf{};

    auto h = MakeBaseReliableHeader();
    ASSERT_TRUE(EncodeHeaderV1(h, buf));

    buf[OffReserved] = 1;

    auto decoded = DecodeHeaderV1(buf);
    EXPECT_FALSE(decoded.has_value());
}

TEST(CodecHeaderV1, ReservedFlagBitsMustBeZero)
{
    std::array<uint8_t, kBufSize> buf{};

    auto h = MakeBaseReliableHeader();
    h.Flags = 0x40; // reserved bit

    EXPECT_FALSE(EncodeHeaderV1(h, buf));
}

TEST(CodecHeaderV1, ChannelTypeOutOfRangeFails)
{
    std::array<uint8_t, kBufSize> buf{};

    auto h = MakeBaseReliableHeader();
    h.ChType = static_cast<ChannelType>(99);

    EXPECT_FALSE(EncodeHeaderV1(h, buf));
}

TEST(CodecHeaderV1, DecodePacketReturnsPayloadSpan)
{
    std::vector<uint8_t> buf(HeaderLenV1 + 10);

    auto h = MakeBaseReliableHeader();
    ASSERT_TRUE(EncodeHeaderV1(h, buf));

    // fake payload
    for (size_t i = 0; i < 10; ++i)
        buf[HeaderLenV1 + i] = static_cast<uint8_t>(i);

    auto pkt = DecodePacketV1(buf);
    ASSERT_TRUE(pkt.has_value());

    EXPECT_EQ(pkt->payload.size(), 10);
    for (size_t i = 0; i < 10; ++i)
        EXPECT_EQ(pkt->payload[i], i);
}

TEST(CodecHeaderV1, ShortBufferFails)
{
    std::array<uint8_t, 10> smallBuf{};

    auto decoded = DecodeHeaderV1(smallBuf);
    EXPECT_FALSE(decoded.has_value());
}