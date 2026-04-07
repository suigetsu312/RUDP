#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Rudp/Protocol.hpp"

namespace Rudp::Session {

enum class SessionRole : std::uint8_t {
  Client = 0,
  Server = 1,
};

enum class ConnectionState : std::uint8_t {
  Closed = 0,
  HandshakeSent = 1,
  HandshakeReceived = 2,
  Established = 3,
  Closing = 4,
  Reset = 5,
};

struct OwnedPacket final {
  Rudp::Header header;
  std::vector<std::byte> payload;
};

struct SendRequest final {
  std::uint32_t channel_id = 0;
  Rudp::ChannelType channel_type = Rudp::ChannelType::Unreliable;
  std::vector<std::byte> payload;
};

struct TxEntry final {
  OwnedPacket packet;
  std::uint64_t first_send_ms = 0;
  std::uint64_t last_send_ms = 0;
  std::uint32_t retry_count = 0;
  bool fast_retx_pending = false;
};

struct SessionEvent final {
  enum class Type {
    DataReceived,
    Connected,
    ConnectionClosed,
    ConnectionReset,
    Error,
  };

  Type type = Type::DataReceived;
  std::uint32_t channel_id = 0;
  Rudp::ChannelType channel_type = Rudp::ChannelType::Unreliable;
  std::vector<std::byte> payload;
  std::string error_message;
};

struct SessionStats final {
  std::uint64_t packets_sent = 0;
  std::uint64_t packets_received = 0;
  std::uint64_t bytes_sent = 0;
  std::uint64_t bytes_received = 0;
  std::uint64_t control_packets_sent = 0;
  std::uint64_t control_packets_received = 0;
  std::uint64_t data_packets_sent = 0;
  std::uint64_t data_packets_received = 0;
  std::uint64_t pings_sent = 0;
  std::uint64_t pongs_sent = 0;
  std::uint64_t pings_received = 0;
  std::uint64_t pongs_received = 0;
  std::uint64_t retransmissions_sent = 0;
  std::optional<std::uint64_t> latest_rtt_ms;
};

struct TxPollResult final {
  std::optional<std::vector<std::byte>> datagram;
  bool fatal_error = false;
  bool retransmission = false;
  std::string error_message;
};

struct TxAckResult final {
  bool acknowledged_fin = false;
};

struct RxPacketResult final {
  bool schedule_ack_only = false;
};

struct TxSessionState final {
  std::uint32_t next_seq = 0;
  std::uint32_t remote_ack = 0;
  std::uint64_t remote_ack_bits = 0;
  std::deque<SendRequest> pending_send;
  std::map<std::uint32_t, TxEntry> inflight;
  bool syn_ack_pending = false;
  bool final_ack_pending = false;
  std::uint64_t final_ack_linger_until_ms = 0;
  bool fin_pending = false;
  bool ack_only_pending = false;
  bool activity_ack_pending = false;
  bool ping_pending = false;
  bool pong_pending = false;
  bool ping_outstanding = false;
  std::uint64_t last_ping_sent_ms = 0;
};

struct RxSessionState final {
  std::uint32_t next_expected = 0;
  std::uint64_t received_bits = 0;
  std::map<std::uint32_t, OwnedPacket> ordered_reorder_buffer;
  std::uint32_t next_ordered_delivery = 0;
  bool ordered_delivery_started = false;
  std::unordered_map<std::uint32_t, std::uint32_t> monotonic_versions;
  std::vector<SessionEvent> pending_events;
};

struct SessionState final {
  SessionRole role = SessionRole::Client;
  std::uint32_t conn_id = 0;
  ConnectionState connection_state = ConnectionState::Closed;
  std::uint64_t last_rx_ms = 0;
  std::uint64_t last_tx_ms = 0;
  SessionStats stats;
  TxSessionState tx;
  RxSessionState rx;
};

}  // namespace Rudp::Session
