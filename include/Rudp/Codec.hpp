#ifndef RUDP_CODEC_HPP
#define RUDP_CODEC_HPP

#include <cstdint>
#include <optional>
#include <span>

#include "Rudp/Protocol.hpp"
#include "Rudp/Utils.hpp"

namespace Rudp::Codec {

struct DecodedPacketV1 {
    Rudp::Protocol::HeaderV1 header;
    std::span<const uint8_t> payload; // bytes after the fixed 28-byte header
};

// Encode fixed 28-byte header (v1.1). Payload is NOT encoded here.
inline bool EncodeHeaderV1(const Rudp::Protocol::HeaderV1& h,
                           std::span<uint8_t> out) noexcept
{
    using namespace Rudp::Protocol;
    using namespace Rudp::Utils;

    if (out.size() < HeaderLenV1) return false;

    // Spec-required fixed values
    // Caller can set these, but encoder enforces them for safety.
    const uint8_t headerLen = HeaderLenV1;
    const uint8_t reserved  = 0;

    // Reserved flag bits must be 0
    if ((h.Flags & 0xC0) != 0) return false; // 0x40/0x80

    // ChannelType range check (0..3)
    const auto ct = static_cast<uint8_t>(h.ChType);
    if (ct > 3) return false;

    // Non-reliable packets MUST set Seq=0 (strict)
    if (!IsReliableChannel(h.ChType) && !IsControlFrame(h.Flags)) {
        if (h.Seq != 0) return false;
    }

    if (!WriteU32BE(out, OffConnId,    h.ConnId))  return false;
    if (!WriteU32BE(out, OffSeq,       h.Seq))     return false;
    if (!WriteU32BE(out, OffAck,       h.Ack))     return false;
    if (!WriteU64BE(out, OffAckBits,   h.AckBits)) return false;
    if (!WriteU32BE(out, OffChannelId, h.ChannelId)) return false;

    out[OffChannelType] = ct;
    out[OffFlags]       = h.Flags;
    out[OffHeaderLen]   = headerLen;
    out[OffReserved]    = reserved;

    return true;
}

inline std::optional<Rudp::Protocol::HeaderV1>
DecodeHeaderV1(std::span<const uint8_t> in) noexcept
{
    using namespace Rudp::Protocol;
    using namespace Rudp::Utils;

    if (in.size() < HeaderLenV1) return std::nullopt;

    HeaderV1 h{};

    if (!ReadU32BE(in, OffConnId,    h.ConnId))  return std::nullopt;
    if (!ReadU32BE(in, OffSeq,       h.Seq))     return std::nullopt;
    if (!ReadU32BE(in, OffAck,       h.Ack))     return std::nullopt;
    if (!ReadU64BE(in, OffAckBits,   h.AckBits)) return std::nullopt;
    if (!ReadU32BE(in, OffChannelId, h.ChannelId)) return std::nullopt;

    const uint8_t ct = in[OffChannelType];
    const uint8_t fl = in[OffFlags];
    const uint8_t hl = in[OffHeaderLen];
    const uint8_t rs = in[OffReserved];

    // Validation (v1.1)
    if (hl != HeaderLenV1) return std::nullopt;
    if (rs != 0) return std::nullopt;
    if ((fl & 0xC0) != 0) return std::nullopt; // reserved flag bits must be 0
    if (ct > 3) return std::nullopt;

    h.ChType    = static_cast<ChannelType>(ct);
    h.Flags     = fl;
    h.HeaderLen = hl;
    h.Reserved  = rs;

    // Non-reliable packets MUST set Seq=0 (strict)
    if (!IsReliableChannel(h.ChType) && !IsControlFrame(h.Flags)) {
        if (h.Seq != 0) return std::nullopt;
    }

    return h;
}

// Decode header + return payload view (payload length is derived from UDP datagram length)
inline std::optional<DecodedPacketV1>
DecodePacketV1(std::span<const uint8_t> in) noexcept
{
    using namespace Rudp::Protocol;

    auto hdr = DecodeHeaderV1(in);
    if (!hdr) return std::nullopt;

    DecodedPacketV1 pkt{};
    pkt.header  = *hdr;
    pkt.payload = in.subspan(HeaderLenV1); // remainder is payload

    return pkt;
}

} // namespace Rudp::Codec

#endif