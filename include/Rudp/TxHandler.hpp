#pragma once

#include <optional>
#include <span>
#include <vector>

#include "Rudp/Codec.hpp"
#include "Rudp/SessionTypes.hpp"

namespace Rudp::Session {

class TxHandler final {
 public:
  void queue_app_data(std::uint32_t channel_id,
                      Rudp::ChannelType channel_type,
                      std::span<const std::byte> payload,
                      TxSessionState& tx);

  [[nodiscard]] TxAckResult on_remote_ack(std::uint32_t ack,
                                          std::uint64_t ack_bits,
                                          TxSessionState& tx);

  [[nodiscard]] TxPollResult poll(std::uint64_t now_ms,
                                  SessionRole role,
                                  std::uint32_t conn_id,
                                  ConnectionState& connection_state,
                                  const RxSessionState& rx,
                                  TxSessionState& tx);

 private:
  [[nodiscard]] std::optional<std::vector<std::byte>> try_build_handshake(
      std::uint64_t now_ms,
      SessionRole role,
      std::uint32_t conn_id,
      ConnectionState& connection_state,
      const RxSessionState& rx,
      TxSessionState& tx);

  [[nodiscard]] TxPollResult try_build_retransmit(std::uint64_t now_ms,
                                                  const RxSessionState& rx,
                                                  TxSessionState& tx);

  [[nodiscard]] std::optional<std::vector<std::byte>> try_build_ack_only(
      std::uint32_t conn_id,
      const RxSessionState& rx,
      TxSessionState& tx);

  [[nodiscard]] std::optional<std::vector<std::byte>> try_build_fresh(
      std::uint64_t now_ms,
      std::uint32_t conn_id,
      const RxSessionState& rx,
      TxSessionState& tx);

  [[nodiscard]] OwnedPacket make_packet_from_request(const SendRequest& req,
                                                     std::uint32_t conn_id,
                                                     const RxSessionState& rx,
                                                     TxSessionState& tx,
                                                     bool assign_reliable_seq);

  [[nodiscard]] std::optional<std::vector<std::byte>> build_control_packet(
      std::uint64_t now_ms,
      std::uint32_t conn_id,
      Rudp::Flags flags,
      const RxSessionState& rx,
      TxSessionState& tx);
};

}  // namespace Rudp::Session
