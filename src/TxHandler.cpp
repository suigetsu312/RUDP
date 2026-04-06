#include "Rudp/TxHandler.hpp"

#include <bit>
#include <utility>

namespace Rudp::Session
{
  namespace
  {

    constexpr std::uint64_t kInitialRtoMs = 250;

    [[nodiscard]] bool is_acknowledged_by_remote(std::uint32_t seq,
                                                 std::uint32_t ack,
                                                 std::uint64_t ack_bits)
    {
      if (Rudp::seq_lt(seq, ack))
      {
        return true;
      }

      if (!Rudp::seq_gt(seq, ack))
      {
        return false;
      }

      const std::uint32_t distance = seq - ack;
      if (distance == 0 || distance > Rudp::kAckBitsWindow)
      {
        return false;
      }

      const auto bit_index = static_cast<unsigned>(distance - 1U);
      return (ack_bits & (1ULL << bit_index)) != 0;
    }

    [[nodiscard]] std::vector<std::byte> copy_payload(
        std::span<const std::byte> payload)
    {
      return std::vector<std::byte>(payload.begin(), payload.end());
    }

    [[nodiscard]] bool consumes_reliable_seq(Rudp::Flags flags)
    {
      return (flags & static_cast<Rudp::Flags>(Rudp::Flag::Syn)) != 0 ||
             (flags & static_cast<Rudp::Flags>(Rudp::Flag::HandshakeAck)) != 0 ||
             (flags & static_cast<Rudp::Flags>(Rudp::Flag::Fin)) != 0;
    }

  } // namespace

  void TxHandler::queue_app_data(std::uint32_t channel_id,
                                 Rudp::ChannelType channel_type,
                                 std::span<const std::byte> payload,
                                 TxSessionState &tx)
  {
    tx.pending_send.push_back(SendRequest{
        .channel_id = channel_id,
        .channel_type = channel_type,
        .payload = copy_payload(payload),
    });
  }

  void TxHandler::on_remote_ack(std::uint32_t ack,
                                std::uint64_t ack_bits,
                                TxSessionState &tx)
  {
    tx.remote_ack = ack;
    tx.remote_ack_bits = ack_bits;

    for (auto it = tx.inflight.begin(); it != tx.inflight.end();)
    {
      if (is_acknowledged_by_remote(it->first, ack, ack_bits))
      {
        it = tx.inflight.erase(it);
        continue;
      }
      ++it;
    }

    if (ack_bits == 0ULL)
    {
      return;
    }

    const unsigned highest_bit =
        63U - static_cast<unsigned>(std::countl_zero(ack_bits));
    const std::uint32_t highest_acked_seq = ack + highest_bit + 1U;

    for (std::uint32_t seq = ack; Rudp::seq_le(seq, highest_acked_seq); ++seq)
    {
      if (is_acknowledged_by_remote(seq, ack, ack_bits))
      {
        continue;
      }

      auto it = tx.inflight.find(seq);
      if (it != tx.inflight.end())
      {
        it->second.fast_retx_pending = true;
      }
    }
  }

  std::optional<std::vector<std::byte>> TxHandler::poll(std::uint64_t now_ms,
                                                        SessionRole role,
                                                        ConnectionState &connection_state,
                                                        const RxSessionState &rx,
                                                        TxSessionState &tx)
  {
    if (auto bytes =
            try_build_handshake(now_ms, role, connection_state, rx, tx);
        bytes.has_value())
    {
      return bytes;
    }
    if (auto bytes = try_build_retransmit(now_ms, rx, tx); bytes.has_value())
    {
      return bytes;
    }
    if (auto bytes = try_build_ack_only(rx, tx); bytes.has_value())
    {
      return bytes;
    }
    if (auto bytes = try_build_fresh(now_ms, rx, tx); bytes.has_value())
    {
      return bytes;
    }
    return std::nullopt;
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_handshake(
      std::uint64_t now_ms,
      SessionRole role,
      ConnectionState &connection_state,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    if (role == SessionRole::Client &&
        connection_state == ConnectionState::Closed)
    {
      connection_state = ConnectionState::HandshakeSent;
      return build_control_packet(now_ms, static_cast<Rudp::Flags>(Rudp::Flag::Syn),
                                  rx, tx);
    }

    if (tx.syn_ack_pending &&
        connection_state == ConnectionState::HandshakeReceived)
    {
      tx.syn_ack_pending = false;
      return build_control_packet(
          now_ms, Rudp::Flag::Syn | Rudp::Flag::HandshakeAck, rx, tx);
    }

    if (tx.final_ack_pending &&
        connection_state == ConnectionState::Established)
    {
      tx.final_ack_pending = false;
      return build_control_packet(
          now_ms, static_cast<Rudp::Flags>(Rudp::Flag::HandshakeAck), rx, tx);
    }

    if (tx.fin_pending && connection_state == ConnectionState::Closing)
    {
      tx.fin_pending = false;
      return build_control_packet(now_ms, static_cast<Rudp::Flags>(Rudp::Flag::Fin),
                                  rx, tx);
    }

    return std::nullopt;
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_retransmit(
      std::uint64_t now_ms,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    for (auto &[seq, entry] : tx.inflight)
    {
      const bool timed_out = now_ms >= entry.last_send_ms + kInitialRtoMs;
      if (!entry.fast_retx_pending && !timed_out)
      {
        continue;
      }

      entry.last_send_ms = now_ms;
      ++entry.retry_count;
      entry.fast_retx_pending = false;

      auto header = entry.packet.header;
      header.ack = rx.next_expected;
      header.ack_bits = rx.received_bits;
      // Ack/AckBits from RX state are copied into every outbound header here.
      entry.packet.header.ack = header.ack;
      entry.packet.header.ack_bits = header.ack_bits;

      // TODO: tune retransmission timeout/backoff policy.
      return Rudp::Codec::encode(header, entry.packet.payload);
    }

    return std::nullopt;
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_ack_only(
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    if (!tx.ack_only_pending)
    {
      return std::nullopt;
    }

    Header header{};
    header.channel_type = Rudp::ChannelType::Unreliable;
    header.ack = rx.next_expected;
    header.ack_bits = rx.received_bits;
    // Ack/AckBits from RX state are copied into every outbound header here.

    tx.ack_only_pending = false;
    return Rudp::Codec::encode(header, {});
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_fresh(
      std::uint64_t now_ms,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    if (tx.pending_send.empty())
    {
      return std::nullopt;
    }

    const SendRequest request = std::move(tx.pending_send.front());
    tx.pending_send.pop_front();

    const bool assign_reliable_seq = Rudp::isReliableChannel(request.channel_type);
    if (assign_reliable_seq &&
        (tx.next_seq - tx.remote_ack) >= Rudp::kReliableWindowSize)
    {
      tx.pending_send.push_front(request);
      return std::nullopt;
    }

    OwnedPacket packet =
        make_packet_from_request(request, rx, tx, assign_reliable_seq);
    auto encoded = Rudp::Codec::encode(packet.header, packet.payload);

    if (assign_reliable_seq)
    {
      TxEntry entry{
          .packet = packet,
          .first_send_ms = now_ms,
          .last_send_ms = now_ms,
          .retry_count = 0,
          .fast_retx_pending = false,
      };
      tx.inflight.emplace(entry.packet.header.seq, std::move(entry));
    }

    return encoded;
  }

  OwnedPacket TxHandler::make_packet_from_request(const SendRequest &req,
                                                  const RxSessionState &rx,
                                                  TxSessionState &tx,
                                                  bool assign_reliable_seq)
  {
    OwnedPacket packet;
    packet.header.channel_id = req.channel_id;
    packet.header.channel_type = req.channel_type;
    packet.header.ack = rx.next_expected;
    packet.header.ack_bits = rx.received_bits;
    // Ack/AckBits from RX state are copied into every outbound header here.

    if (assign_reliable_seq)
    {
      // Reliable sequence numbers are assigned only when a packet is selected for
      // outbound transmission, not during queue_send().
      packet.header.seq = tx.next_seq++;
    }
    else
    {
      packet.header.seq = 0;
    }

    packet.payload = req.payload;
    return packet;
  }

  std::optional<std::vector<std::byte>> TxHandler::build_control_packet(
      std::uint64_t now_ms,
      Rudp::Flags flags,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    const bool assign_reliable_seq = consumes_reliable_seq(flags);
    if (assign_reliable_seq &&
        (tx.next_seq - tx.remote_ack) >= Rudp::kReliableWindowSize)
    {
      return std::nullopt;
    }

    Header header{};
    header.channel_type = Rudp::ChannelType::Unreliable;
    header.flags = flags;
    header.ack = rx.next_expected;
    header.ack_bits = rx.received_bits;

    if (assign_reliable_seq)
    {
      header.seq = tx.next_seq++;
    }

    auto encoded = Rudp::Codec::encode(header, {});
    if (!assign_reliable_seq)
    {
      return encoded;
    }

    TxEntry entry{
        .packet = OwnedPacket{.header = header, .payload = {}},
        .first_send_ms = now_ms,
        .last_send_ms = now_ms,
        .retry_count = 0,
        .fast_retx_pending = false,
    };
    tx.inflight.emplace(header.seq, std::move(entry));
    return encoded;
  }

} // namespace Rudp::Session
