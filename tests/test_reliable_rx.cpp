#include <gtest/gtest.h>
#include "Rudp/ReliableRx.hpp"

using Rudp::ReliableRx;

TEST(ReliableRx, AdvancesAckOnContiguousReception) {
    ReliableRx rx(/*initial_ack=*/10);

    rx.OnRxSeq(10);
    EXPECT_EQ(rx.Ack(), 11u);
    EXPECT_EQ(rx.AckBits(), 0ULL);

    rx.OnRxSeq(11);
    EXPECT_EQ(rx.Ack(), 12u);
    EXPECT_EQ(rx.AckBits(), 0ULL);

    rx.OnRxSeq(12);
    EXPECT_EQ(rx.Ack(), 13u);
    EXPECT_EQ(rx.AckBits(), 0ULL);
}

TEST(ReliableRx, MarksOutOfOrderInForwardBitmap) {
    ReliableRx rx(/*initial_ack=*/10);

    rx.OnRxSeq(11); // bit0
    EXPECT_EQ(rx.Ack(), 10u);
    EXPECT_EQ(rx.AckBits(), 0b1ULL);

    rx.OnRxSeq(13); // delta=3 => bit2
    EXPECT_EQ(rx.Ack(), 10u);
    EXPECT_EQ(rx.AckBits(), (0b1ULL | (1ULL << 2)));
}

TEST(ReliableRx, FillsGapAndConsumesBitmapToAdvanceAck) {
    ReliableRx rx(/*initial_ack=*/10);

    rx.OnRxSeq(11); // bit0
    rx.OnRxSeq(12); // bit1
    rx.OnRxSeq(14); // bit3
    EXPECT_EQ(rx.Ack(), 10u);
    EXPECT_EQ(rx.AckBits(), (1ULL << 0) | (1ULL << 1) | (1ULL << 3));

    rx.OnRxSeq(10);

    EXPECT_EQ(rx.Ack(), 13u);
    EXPECT_EQ(rx.AckBits(), 0b1ULL); // 14 becomes Ack+1
}

TEST(ReliableRx, IgnoresPacketsBelowAck) {
    ReliableRx rx(/*initial_ack=*/100);

    rx.OnRxSeq(100);
    rx.OnRxSeq(101);
    EXPECT_EQ(rx.Ack(), 102u);

    rx.OnRxSeq(100);
    rx.OnRxSeq(101);
    EXPECT_EQ(rx.Ack(), 102u);
    EXPECT_EQ(rx.AckBits(), 0ULL);
}

TEST(ReliableRx, IgnoresOutOfWindowFuturePackets) {
    ReliableRx rx(/*initial_ack=*/10);

    rx.OnRxSeq(75); // delta=65 -> ignore
    EXPECT_EQ(rx.Ack(), 10u);
    EXPECT_EQ(rx.AckBits(), 0ULL);

    rx.OnRxSeq(74); // delta=64 -> bit63
    EXPECT_EQ(rx.Ack(), 10u);
    EXPECT_EQ(rx.AckBits(), (1ULL << 63));
}