#ifndef RUDP_PROTOCOL_HPP
#define RUDP_PROTOCOL_HPP

#include <cstddef>
#include <cstdint>

namespace Rudp::Protocol {

// Wire byte order: Big-endian (network order)
// Fixed header: 28 bytes (v1.1)

inline constexpr uint8_t VersionV1_1 = 1; // spec version tag (not on-wire unless you add options)

inline constexpr std::size_t AckBitsWidth = 64;
inline constexpr std::size_t HeaderLenV1  = 28;

// ChannelType values (v1.1)
enum class ChannelType : uint8_t {
    ReliableOrdered   = 0,
    ReliableUnordered = 1,
    Unreliable        = 2,
    MonotonicState    = 3,
};

// Flags (v1.1)
enum class Flags : uint8_t {
    Syn    = 0x01,
    HskAck = 0x02,
    Fin    = 0x04,
    Rst    = 0x08,
    Ping   = 0x10,
    Pong   = 0x20,
};

inline constexpr uint8_t ToU8(Flags f) noexcept { return static_cast<uint8_t>(f); }

// Logical header fields (not an on-wire struct; use Codec to encode/decode)
struct HeaderV1 {
    uint32_t   ConnId;
    uint32_t   Seq;        // reliable seq; MUST be 0 for non-reliable packets
    uint32_t   Ack;        // next expected reliable seq
    uint64_t   AckBits;    // forward bitmap: bit i => seq = Ack + i + 1
    uint32_t   ChannelId;
    ChannelType ChType;
    uint8_t    Flags;      // bitset (see enum Flags)
    uint8_t    HeaderLen;  // MUST be 28 in v1.1
    uint8_t    Reserved;   // MUST be 0
};

// Wire layout offsets (bytes):
//  0  : U32 ConnId
//  4  : U32 Seq
//  8  : U32 Ack
// 12  : U64 AckBits
// 20  : U32 ChannelId
// 24  : U8  ChannelType
// 25  : U8  Flags
// 26  : U8  HeaderLen
// 27  : U8  Reserved
inline constexpr std::size_t OffConnId     = 0;
inline constexpr std::size_t OffSeq        = 4;
inline constexpr std::size_t OffAck        = 8;
inline constexpr std::size_t OffAckBits    = 12;
inline constexpr std::size_t OffChannelId  = 20;
inline constexpr std::size_t OffChannelType= 24;
inline constexpr std::size_t OffFlags      = 25;
inline constexpr std::size_t OffHeaderLen  = 26;
inline constexpr std::size_t OffReserved   = 27;

// Modulo-2^32 ordering helpers
inline constexpr bool SeqLt(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) < 0;
}
inline constexpr bool SeqLe(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) <= 0;
}
inline constexpr bool SeqGt(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) > 0;
}
inline constexpr bool SeqGe(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) >= 0;
}

// Predicate: whether this packet participates in reliable state updates
inline constexpr bool IsReliableChannel(ChannelType t) noexcept {
    return t == ChannelType::ReliableOrdered || t == ChannelType::ReliableUnordered;
}

// Connection-level control frames (handshake/FIN) also consume reliable seq.
// In v1.1, this is represented by Flags (SYN/HSK_ACK/FIN/RST).
inline constexpr bool IsControlFrame(uint8_t flags) noexcept {
    return (flags & (ToU8(Flags::Syn) | ToU8(Flags::HskAck) | ToU8(Flags::Fin) | ToU8(Flags::Rst))) != 0;
}

} // namespace Rudp::Protocol
#endif