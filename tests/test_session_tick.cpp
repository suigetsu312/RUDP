#include <gtest/gtest.h>
#include "Rudp/Session.hpp"

using namespace Rudp;

static Protocol::HeaderV1 MakeHdr(uint32_t connId, uint8_t ch, uint16_t len) {
    Protocol::HeaderV1 h{};
    h.ConnId = connId;
    h.ChannelId = ch;
    h.PayloadLen = len;
    return h;
}

TEST(SessionTick, ClientHandshakeTimeoutTransitionsToTimedOut) {
    SessionConfig cfg{};
    cfg.HandshakeTimeoutMs = 100;
    Session c(/*isServer=*/false, cfg);

    ASSERT_TRUE(c.AppConnect(10).has_value());
    EXPECT_EQ(c.State(), ConnState::SynSent);

    c.OnTick(10 + 99);
    EXPECT_EQ(c.State(), ConnState::SynSent);

    c.OnTick(10 + 100);
    EXPECT_EQ(c.State(), ConnState::TimedOut);
}

TEST(SessionTick, ServerHalfOpenTimeoutClosesSession) {
    SessionConfig cfg{};
    cfg.HandshakeTimeoutMs = 100;
    Session s(/*isServer=*/true, cfg);

    uint8_t syn[1] = { static_cast<uint8_t>(Protocol::ControlType::Syn) };
    auto tx = s.OnRx(10, MakeHdr(0, Protocol::ChannelControl, 1), syn);
    ASSERT_TRUE(tx.has_value());
    EXPECT_EQ(s.State(), ConnState::SynReceived);
    EXPECT_NE(s.ConnId(), 0u);

    s.OnTick(10 + 100);
    EXPECT_EQ(s.State(), ConnState::Closed);
    EXPECT_EQ(s.ConnId(), 0u);
}

TEST(SessionTick, EstablishedIdleTimeoutTransitionsToTimedOut) {
    SessionConfig cfg{};
    cfg.IdleTimeoutMs = 100;
    cfg.HeartbeatIntervalMs = 1000; // avoid heartbeat interfering
    Session c(/*isServer=*/false, cfg);

    // Move client to Established quickly by faking SYN-ACK
    ASSERT_TRUE(c.AppConnect(10).has_value());
    uint8_t synAck[1] = { static_cast<uint8_t>(Protocol::ControlType::SynAck) };
    auto tx = c.OnRx(20, MakeHdr(123, Protocol::ChannelControl, 1), synAck);
    ASSERT_TRUE(tx.has_value());
    EXPECT_EQ(c.State(), ConnState::Established);

    // lastRxMs_ was set at 20. At 20+99 still alive; at 20+100 => timeout
    c.OnTick(20 + 99);
    EXPECT_EQ(c.State(), ConnState::Established);

    c.OnTick(20 + 100);
    EXPECT_EQ(c.State(), ConnState::TimedOut);
}

TEST(SessionTick, EstablishedHeartbeatEmitsAckOnly) {
    SessionConfig cfg{};
    cfg.IdleTimeoutMs = 1000;       // avoid idle timeout
    cfg.HeartbeatIntervalMs = 50;
    Session c(/*isServer=*/false, cfg);

    ASSERT_TRUE(c.AppConnect(10).has_value());
    uint8_t synAck[1] = { static_cast<uint8_t>(Protocol::ControlType::SynAck) };
    ASSERT_TRUE(c.OnRx(20, MakeHdr(123, Protocol::ChannelControl, 1), synAck).has_value());
    EXPECT_EQ(c.State(), ConnState::Established);

    // lastTxMs_ was set at 20 (from OnRx returning Ack), so before 20+50 -> no heartbeat.
    auto t1 = c.OnTick(20 + 49);
    EXPECT_FALSE(t1.has_value());

    auto t2 = c.OnTick(20 + 50);
    ASSERT_TRUE(t2.has_value());
    EXPECT_EQ(t2->Kind, TxKind::Ack);
}
