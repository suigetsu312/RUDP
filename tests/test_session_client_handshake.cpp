#include <gtest/gtest.h>
#include "Rudp/Session.hpp"

using namespace Rudp;

// Test helper: build a minimal header for tests.
static Protocol::HeaderV1 MakeHdr(uint32_t connId, uint8_t ch, uint16_t len) {
    Protocol::HeaderV1 h{};
    h.ConnId = connId;
    h.Seq = 0;
    h.Ack = 0;
    h.AckBits = 0;
    h.TimestampMs = 0;
    h.ChannelId = ch;
    h.PayloadLen = len;
    return h;
}

// AppConnect() initiates the handshake.
TEST(SessionClient, AppConnectSendsSyn) {
    SessionConfig cfg{};
    Session s(/*isServer=*/false, cfg);

    auto tx = s.AppConnect(100);
    ASSERT_TRUE(tx.has_value());
    EXPECT_EQ(s.State(), ConnState::SynSent);
    EXPECT_EQ(tx->Kind, TxKind::Ctrl);
    EXPECT_EQ(tx->Ctrl, Protocol::ControlType::Syn);
}

// Receiving SYN-ACK transitions to Established and emits ack-only.
TEST(SessionClient, SynAckTransitionsToEstablishedAndSendsAckOnly) {
    SessionConfig cfg{};
    Session s(/*isServer=*/false, cfg);

    ASSERT_TRUE(s.AppConnect(100).has_value());
    EXPECT_EQ(s.State(), ConnState::SynSent);

    uint8_t payload[1] = { static_cast<uint8_t>(Protocol::ControlType::SynAck) };
    auto hdr = MakeHdr(/*connId=*/123, Protocol::ChannelControl, /*len=*/1);

    auto tx = s.OnRx(120, hdr, payload);
    ASSERT_TRUE(tx.has_value());
    EXPECT_EQ(s.State(), ConnState::Established);
    EXPECT_EQ(s.ConnId(), 123u);
    EXPECT_EQ(tx->Kind, TxKind::Ack);
}
