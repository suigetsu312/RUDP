#ifndef RUDP_CODEC_HPP
#define RUDP_CODEC_HPP
#include <cstdint>
#include <optional>
#include <span>

#include "Rudp/Protocol.hpp"
#include "Rudp/Utils.hpp"

namespace Rudp::Codec {

inline bool EncodeHeaderV1(const Rudp::Protocol::HeaderV1& header,
                           std::span<uint8_t> out) noexcept
{
    using namespace Rudp::Protocol;
    using namespace Rudp::Utils;

    if (out.size() < HeaderV1Length) return false;

    if (!WriteU32BE(out, OffConnId, header.ConnId)) return false;
    if (!WriteU32BE(out, OffSeq, header.Seq)) return false;
    if (!WriteU32BE(out, OffAck, header.Ack)) return false;
    if (!WriteU64BE(out, OffAckBits, header.AckBits)) return false;
    if (!WriteU32BE(out, OffTimestampMs, header.TimestampMs)) return false;

    out[OffChannelId] = header.ChannelId;
    out[OffReserved]  = 0;

    if (!WriteU16BE(out, OffPayloadLen, header.PayloadLen)) return false;

    return true;
}

// Validates: HeaderV1Length + PayloadLen <= datagram length
inline std::optional<Rudp::Protocol::HeaderV1>
DecodeHeaderV1(std::span<const uint8_t> in) noexcept
{
    using namespace Rudp::Protocol;
    using namespace Rudp::Utils;

    if (in.size() < HeaderV1Length) return std::nullopt;

    HeaderV1 header{};

    if (!ReadU32BE(in, OffConnId, header.ConnId)) return std::nullopt;
    if (!ReadU32BE(in, OffSeq, header.Seq)) return std::nullopt;
    if (!ReadU32BE(in, OffAck, header.Ack)) return std::nullopt;
    if (!ReadU64BE(in, OffAckBits, header.AckBits)) return std::nullopt;
    if (!ReadU32BE(in, OffTimestampMs, header.TimestampMs)) return std::nullopt;

    header.ChannelId = in[OffChannelId];

    uint16_t payloadLen = 0;
    if (!ReadU16BE(in, OffPayloadLen, payloadLen)) return std::nullopt;
    header.PayloadLen = payloadLen;

    if (HeaderV1Length + static_cast<size_t>(header.PayloadLen) > in.size())
        return std::nullopt;

    return header;
}

} // namespace Rudp::Codec
#endif