#pragma once

#include <optional>
#include <span>
#include <vector>

#include "Rudp/Codec.hpp"
#include "Rudp/ConnectionStateMachine.hpp"
#include "Rudp/RxHandler.hpp"
#include "Rudp/TxHandler.hpp"

namespace Rudp::Session {

class Session final {
 public:
  explicit Session(SessionRole role = SessionRole::Client)
      : state_(SessionState{.role = role,
                            .connection_state = ConnectionState::Closed,
                            .tx = {},
                            .rx = {}}) {}

  void queue_send(std::uint32_t channel_id,
                  Rudp::ChannelType channel_type,
                  std::span<const std::byte> payload);

  [[nodiscard]] std::optional<std::vector<std::byte>> poll_tx(
      std::uint64_t now_ms);

  void on_datagram_received(std::span<const std::byte> bytes,
                            std::uint64_t now_ms);

  void request_close();

  [[nodiscard]] std::vector<SessionEvent> drain_events();

  [[nodiscard]] SessionRole role() const noexcept { return state_.role; }
  [[nodiscard]] ConnectionState connection_state() const noexcept {
    return state_.connection_state;
  }
  [[nodiscard]] std::uint32_t next_expected_seq() const noexcept {
    return state_.rx.next_expected;
  }

 private:
  void apply_connection_decision(const Rudp::PacketView& packet,
                                 const ConnectionDecision& decision);

  SessionState state_;
  TxHandler tx_handler_;
  RxHandler rx_handler_;
};

}  // namespace Rudp::Session
