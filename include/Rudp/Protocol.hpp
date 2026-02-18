#ifndef RUDP_PROTOCOL_HPP
#define RUDP_PROTOCOL_HPP
#include <cstddef>
#include <cstdint>

namespace Rudp::Protocol {

// Wire byte order: Big-endian (network order)

inline constexpr uint8_t VersionV1 = 1;

// Reserved channel for control plane (SYN/RESET/FIN...)
inline constexpr uint8_t ChannelControl = 0xFF;

// AckBits window size
inline constexpr size_t SackWindowBits = 64;

// Logical header fields (not an on-wire struct; use Codec to encode/decode)
struct HeaderV1 {
    uint32_t ConnId;       // session id
    uint32_t Seq;          // packet seq (single sequence space per connection)
    uint32_t Ack;          // max contiguous received seq
    uint64_t AckBits;      // bit0=Ack-1 ... bit63=Ack-64
    uint32_t TimestampMs;  // sender timestamp (ms)
    uint8_t  ChannelId;    // one packet = one channel (v1)
    uint16_t PayloadLen;   // payload bytes; 0 => ack-only / keepalive
};

// Wire layout offsets (bytes):
//  0  : U32 ConnId
//  4  : U32 Seq
//  8  : U32 Ack
// 12  : U64 AckBits
// 20  : U32 TimestampMs
// 24  : U8  ChannelId
// 25  : U8  Reserved (0)
// 26  : U16 PayloadLen
inline constexpr size_t HeaderV1Length = 28;

inline constexpr size_t OffConnId      = 0;
inline constexpr size_t OffSeq         = 4;
inline constexpr size_t OffAck         = 8;
inline constexpr size_t OffAckBits     = 12;
inline constexpr size_t OffTimestampMs = 20;
inline constexpr size_t OffChannelId   = 24;
inline constexpr size_t OffReserved    = 25;
inline constexpr size_t OffPayloadLen  = 26;

// Optional control types (if ChannelId == ChannelControl, payload[0] can be this)
enum class ControlType : uint8_t {
    Syn    = 1,
    SynAck = 2,
    Reset  = 3,
    Fin    = 4,
};

// 32-bit wrap-safe comparison
inline constexpr bool IsSeqNewer(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) > 0;
}

} // namespace Rudp::Protocol
#endif