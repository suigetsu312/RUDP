#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Rudp {

constexpr std::uint8_t kHeaderLength = 28;
constexpr std::size_t kAckBitsWindow = 64;
constexpr std::size_t kReliableWindowSize = 64;

enum class ChannelType : std::uint8_t {
  ReliableOrdered = 0,
  ReliableUnordered = 1,
  Unreliable = 2,
  MonotonicState = 3,
};

enum class Flag : std::uint8_t {
  Syn = 0x01,
  Fin = 0x02,
  Rst = 0x04,
  Ping = 0x08,
  Pong = 0x10,
  Ack = 0x20,
};

using Flags = std::uint8_t;

struct Header final {
  std::uint32_t conn_id = 0;
  std::uint32_t seq = 0;
  std::uint32_t ack = 0;
  std::uint64_t ack_bits = 0;
  std::uint32_t channel_id = 0;
  ChannelType channel_type = ChannelType::Unreliable;
  Flags flags = 0;
  std::uint8_t header_len = kHeaderLength;
  std::uint8_t reserved = 0;

  [[nodiscard]] bool hasFlag(Flag flag) const noexcept;
};

struct PacketView final {
  Header header;
  std::span<const std::byte> payload;
};

[[nodiscard]] constexpr bool seq_lt(std::uint32_t lhs,
                                    std::uint32_t rhs) noexcept {
  return static_cast<std::int32_t>(lhs - rhs) < 0;
}

[[nodiscard]] constexpr bool seq_le(std::uint32_t lhs,
                                    std::uint32_t rhs) noexcept {
  return static_cast<std::int32_t>(lhs - rhs) <= 0;
}

[[nodiscard]] constexpr bool seq_gt(std::uint32_t lhs,
                                    std::uint32_t rhs) noexcept {
  return seq_lt(rhs, lhs);
}

[[nodiscard]] constexpr bool seq_ge(std::uint32_t lhs,
                                    std::uint32_t rhs) noexcept {
  return seq_le(rhs, lhs);
}

[[nodiscard]] constexpr bool isReliableChannel(ChannelType type) noexcept {
  return type == ChannelType::ReliableOrdered ||
         type == ChannelType::ReliableUnordered;
}

[[nodiscard]] constexpr Flags operator|(Flag lhs, Flag rhs) noexcept {
  return static_cast<Flags>(static_cast<Flags>(lhs) | static_cast<Flags>(rhs));
}

[[nodiscard]] constexpr Flags operator|(Flags lhs, Flag rhs) noexcept {
  return static_cast<Flags>(lhs | static_cast<Flags>(rhs));
}

}  // namespace Rudp
