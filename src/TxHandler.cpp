#include "Rudp/TxHandler.hpp"
#include "Rudp/Config.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace Rudp::Session
{
  namespace
  {
    constexpr std::uint32_t kInternalProbeChannelId = 0;
    constexpr Rudp::ChannelType kInternalProbeChannelType =
        Rudp::ChannelType::Unreliable;


    [[nodiscard]] bool is_acknowledged_by_remote(std::uint32_t seq,
                                                 std::uint32_t ack,
                                                 std::uint64_t ack_bits)
    {
      if (Rudp::seq_lt(seq, ack))
      {
        return true;
      }

      if (!Rudp::seq_gt(seq, ack)) // case by seq equals ack
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
             (flags & static_cast<Rudp::Flags>(Rudp::Flag::Fin)) != 0;
    }

    [[nodiscard]] std::uint64_t retransmit_timeout_for(
        std::uint32_t retry_count)
    {
      const auto& transport = Rudp::Config::current().transport;
      const auto clamped_retry_count = std::min<std::uint32_t>(retry_count, 4U);
      const auto rto = transport.initial_rto_ms << clamped_retry_count;
      return std::min(rto, transport.max_rto_ms);
    }

    [[nodiscard]] Header make_internal_probe_header(std::uint32_t conn_id,
                                                    Rudp::Flags flags,
                                                    const RxSessionState& rx)
    {
      Header header{};
      header.conn_id = conn_id;
      header.flags = flags;
      header.channel_id = kInternalProbeChannelId;
      header.channel_type = kInternalProbeChannelType;
      header.ack = rx.next_expected;
      header.ack_bits = rx.received_bits;
      return header;
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

  TxAckResult TxHandler::on_remote_ack(std::uint32_t ack,
                                       std::uint64_t ack_bits,
                                       TxSessionState &tx)
  {
    TxAckResult result{};
    tx.remote_ack = ack;
    tx.remote_ack_bits = ack_bits;

    for (auto it = tx.inflight.begin(); it != tx.inflight.end();)
    {
      if (is_acknowledged_by_remote(it->first, ack, ack_bits))
      {
        if (it->second.packet.header.hasFlag(Rudp::Flag::Fin))
        {
          result.acknowledged_fin = true;
        }
        it = tx.inflight.erase(it);
        continue;
      }
      ++it;
    }

    if (ack_bits == 0ULL)
    {
      return result;
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
        ++it->second.gap_evidence_count;
        if (it->second.gap_evidence_count >=
            Rudp::Config::current().transport.fast_retx_evidence_threshold)
        {
          it->second.fast_retx_pending = true;
        }
      }
    }
    return result;
  }

  TxPollResult TxHandler::poll(std::uint64_t now_ms,
                               SessionRole role,
                               std::uint32_t conn_id,
                               ConnectionState &connection_state,
                               const RxSessionState &rx,
                               TxSessionState &tx)
  {
    if (auto bytes =
            try_build_handshake(now_ms, role, conn_id, connection_state, rx,
                                tx);
        bytes.has_value())
    {
      return TxPollResult{
          .datagram = std::move(bytes),
          .fatal_error = false,
          .retransmission = false,
          .error_message = {},
      };
    }
    if (auto result = try_build_retransmit(now_ms, rx, tx);
        result.fatal_error || result.datagram.has_value())
    {
      return result;
    }
    if (auto bytes = try_build_probe_lane(conn_id, rx, tx); bytes.has_value())
    {
      return TxPollResult{
          .datagram = std::move(bytes),
          .fatal_error = false,
          .retransmission = false,
          .error_message = {},
      };
    }
    if (auto bytes = try_build_ack_only(conn_id, rx, tx); bytes.has_value())
    {
      return TxPollResult{
          .datagram = std::move(bytes),
          .fatal_error = false,
          .retransmission = false,
          .error_message = {},
      };
    }
    if (auto bytes = try_build_fresh(now_ms, conn_id, rx, tx); bytes.has_value())
    {
      return TxPollResult{
          .datagram = std::move(bytes),
          .fatal_error = false,
          .retransmission = false,
          .error_message = {},
      };
    }
    return {};
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_handshake(
      std::uint64_t now_ms,
      SessionRole role,
      std::uint32_t conn_id,
      ConnectionState &connection_state,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    if (role == SessionRole::Client &&
        connection_state == ConnectionState::Closed)
    {
      connection_state = ConnectionState::HandshakeSent;
      return build_control_packet(now_ms, conn_id,
                                  static_cast<Rudp::Flags>(Rudp::Flag::Syn),
                                  rx, tx);
    }

    if (tx.syn_ack_pending &&
        connection_state == ConnectionState::HandshakeReceived)
    {
      if (auto packet = build_control_packet(
          now_ms, conn_id, Rudp::Flag::Syn | Rudp::Flag::Ack, rx, tx);
          packet.has_value())
      {
        tx.syn_ack_pending = false;
        return packet;
      }
      return std::nullopt;
    }

    if (tx.final_ack_pending &&
        connection_state == ConnectionState::Established)
    {
      if (auto packet = build_control_packet(
          now_ms, conn_id, static_cast<Rudp::Flags>(Rudp::Flag::Ack), rx, tx);
          packet.has_value())
      {
        tx.final_ack_pending = false;
        tx.final_ack_linger_until_ms =
            now_ms + Rudp::Config::current().transport.handshake_linger_ms;
        return packet;
      }
      return std::nullopt;
    }

    if (tx.fin_pending && connection_state == ConnectionState::Closing)
    {
      if (auto packet = build_control_packet(
              now_ms, conn_id,
              static_cast<Rudp::Flags>(Rudp::Flag::Fin), rx, tx);
          packet.has_value())
      {
        tx.fin_pending = false;
        return packet;
      }
      return std::nullopt;
    }

    return std::nullopt;
  }

  TxPollResult TxHandler::try_build_retransmit(std::uint64_t now_ms,
                                               const RxSessionState &rx,
                                               TxSessionState &tx)
  {
    for (auto it = tx.inflight.begin(); it != tx.inflight.end();)
    {
      auto &[seq, entry] = *it;
      static_cast<void>(seq);

      if (entry.retry_count >=
          Rudp::Config::current().transport.max_retransmit_count)
      {
        tx.inflight.erase(it);
        return TxPollResult{
            .datagram = std::nullopt,
            .fatal_error = true,
            .retransmission = false,
            .error_message = "retransmission retry limit exceeded",
        };
      }

      const auto current_rto_ms = retransmit_timeout_for(entry.retry_count);
      const bool timed_out = now_ms >= entry.last_send_ms + current_rto_ms;
      if (!entry.fast_retx_pending && !timed_out)
      {
        ++it;
        continue;
      }

      auto header = entry.packet.header;
      header.ack = rx.next_expected;
      header.ack_bits = rx.received_bits;
      // Ack/AckBits from RX state are copied into every outbound header here.
      entry.packet.header.ack = header.ack;
      entry.packet.header.ack_bits = header.ack_bits;

      auto encoded = Rudp::Codec::encode(header, entry.packet.payload);
      entry.last_send_ms = now_ms;
      ++entry.retry_count;
      entry.gap_evidence_count = 0;
      entry.fast_retx_pending = false;
      return TxPollResult{
          .datagram = std::move(encoded),
          .fatal_error = false,
          .retransmission = true,
          .error_message = {},
      };
    }

    return {};
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_ack_only(
      std::uint32_t conn_id,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    if (!tx.ack_only_pending)
    {
      return std::nullopt;
    }

    Header header{};
    header.conn_id = conn_id;
    header.flags = static_cast<Rudp::Flags>(Rudp::Flag::Ack);
    header.channel_type = Rudp::ChannelType::Unreliable;
    header.ack = rx.next_expected;
    header.ack_bits = rx.received_bits;
    // Pure ACK packets do not carry payload but still use the ACK control flag
    // so receivers do not treat them as empty application data.

    tx.ack_only_pending = false;
    return Rudp::Codec::encode(header, {});
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_fresh(
      std::uint64_t now_ms,
      std::uint32_t conn_id,
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
        make_packet_from_request(request, conn_id, rx, tx, assign_reliable_seq);
    auto encoded = Rudp::Codec::encode(packet.header, packet.payload);

    if (assign_reliable_seq)
    {
      TxEntry entry{
          .packet = packet,
          .first_send_ms = now_ms,
          .last_send_ms = now_ms,
          .retry_count = 0,
          .gap_evidence_count = 0,
          .fast_retx_pending = false,
      };
      tx.inflight.emplace(entry.packet.header.seq, std::move(entry));
    }

    return encoded;
  }

  std::optional<std::vector<std::byte>> TxHandler::try_build_probe_lane(
      std::uint32_t conn_id,
      const RxSessionState &rx,
      TxSessionState &tx)
  {
    if (tx.probe.pong_pending)
    {
      auto header = make_internal_probe_header(
          conn_id, static_cast<Rudp::Flags>(Rudp::Flag::Pong), rx);
      tx.probe.pong_pending = false;
      return Rudp::Codec::encode(header, {});
    }

    if (!tx.probe.ping_pending)
    {
      return std::nullopt;
    }

    auto header = make_internal_probe_header(
        conn_id, static_cast<Rudp::Flags>(Rudp::Flag::Ping), rx);
    tx.probe.ping_pending = false;
    return Rudp::Codec::encode(header, {});
  }

  OwnedPacket TxHandler::make_packet_from_request(const SendRequest &req,
                                                  std::uint32_t conn_id,
                                                  const RxSessionState &rx,
                                                  TxSessionState &tx,
                                                  bool assign_reliable_seq)
  {
    OwnedPacket packet;
    packet.header.conn_id = conn_id;
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
      std::uint32_t conn_id,
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
    header.conn_id = conn_id;
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
        .gap_evidence_count = 0,
        .fast_retx_pending = false,
    };
    tx.inflight.emplace(header.seq, std::move(entry));
    return encoded;
  }

} // namespace Rudp::Session
