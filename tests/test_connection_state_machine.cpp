#include <gtest/gtest.h>

#include "Rudp/ConnectionStateMachine.hpp"

namespace {

using Rudp::Session::ConnectionState;
using Rudp::Session::ControlKind;
using Rudp::Session::SessionRole;

// Verifies a server in Closed moves into HandshakeReceived and schedules a
// SYN-ACK when it receives a SYN.
TEST(ConnectionStateMachineTest, ServerClosedAndSynSchedulesSynAck) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Server, ConnectionState::Closed, ControlKind::Syn);

  EXPECT_TRUE(decision.valid);
  ASSERT_TRUE(decision.next_state.has_value());
  EXPECT_EQ(*decision.next_state, ConnectionState::HandshakeReceived);
  EXPECT_TRUE(decision.schedule_syn_ack);
}

// Verifies the server finishes the handshake when it receives the final ACK
// while waiting in HandshakeReceived.
TEST(ConnectionStateMachineTest, ServerHandshakeReceivedAndAckEstablishes) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Server, ConnectionState::HandshakeReceived,
      ControlKind::Ack);

  EXPECT_TRUE(decision.valid);
  ASSERT_TRUE(decision.next_state.has_value());
  EXPECT_EQ(*decision.next_state, ConnectionState::Established);
  EXPECT_TRUE(decision.emit_connected);
}

// Verifies the client finishes the handshake when a SYN-ACK arrives after it
// has already sent SYN.
TEST(ConnectionStateMachineTest, ClientHandshakeSentAndSynAckEstablishes) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Client, ConnectionState::HandshakeSent,
      ControlKind::SynAck);

  EXPECT_TRUE(decision.valid);
  ASSERT_TRUE(decision.next_state.has_value());
  EXPECT_EQ(*decision.next_state, ConnectionState::Established);
  EXPECT_TRUE(decision.schedule_final_ack);
  EXPECT_TRUE(decision.emit_connected);
}

// Verifies an unexpected SYN does not silently restart an established
// connection.
TEST(ConnectionStateMachineTest, EstablishedAndUnexpectedSynIsInvalid) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Server, ConnectionState::Established, ControlKind::Syn);

  EXPECT_FALSE(decision.valid);
  EXPECT_FALSE(decision.next_state.has_value());
  EXPECT_FALSE(decision.error_message.empty());
}

// Verifies FIN transitions the connection into Closing and requests an ACK
// response.
TEST(ConnectionStateMachineTest, FinTransitionsToClosingAndSchedulesAck) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Client, ConnectionState::Established, ControlKind::Fin);

  EXPECT_TRUE(decision.valid);
  ASSERT_TRUE(decision.next_state.has_value());
  EXPECT_EQ(*decision.next_state, ConnectionState::Closing);
  EXPECT_TRUE(decision.schedule_ack_only);
  EXPECT_TRUE(decision.emit_connection_closed);
}

// Verifies RST always forces the connection into Reset and emits reset intent.
TEST(ConnectionStateMachineTest, RstTransitionsToReset) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Client, ConnectionState::Established, ControlKind::Rst);

  EXPECT_TRUE(decision.valid);
  ASSERT_TRUE(decision.next_state.has_value());
  EXPECT_EQ(*decision.next_state, ConnectionState::Reset);
  EXPECT_TRUE(decision.emit_connection_reset);
}

// Verifies explicitly invalid control classifications are rejected and do not
// produce state changes.
TEST(ConnectionStateMachineTest, InvalidControlKindIsRejected) {
  const auto decision = Rudp::Session::decide_connection_transition(
      SessionRole::Client, ConnectionState::Established, ControlKind::Invalid);

  EXPECT_FALSE(decision.valid);
  EXPECT_FALSE(decision.next_state.has_value());
  EXPECT_FALSE(decision.error_message.empty());
}

}  // namespace
