#include "Rudp/Utils.hpp"

namespace Rudp::Utils {

std::uint32_t readU32(std::span<const std::byte> bytes,
                      std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(
              std::to_integer<std::uint8_t>(bytes[offset + 0]))
          << 24) |
         (static_cast<std::uint32_t>(
              std::to_integer<std::uint8_t>(bytes[offset + 1]))
          << 16) |
         (static_cast<std::uint32_t>(
              std::to_integer<std::uint8_t>(bytes[offset + 2]))
          << 8) |
         static_cast<std::uint32_t>(
             std::to_integer<std::uint8_t>(bytes[offset + 3]));
}

std::uint64_t readU64(std::span<const std::byte> bytes,
                      std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < 8; ++index) {
    value =
        (value << 8) |
        static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index]));
  }
  return value;
}

void writeU32(std::span<std::byte> bytes, std::size_t offset,
              std::uint32_t value) noexcept {
  bytes[offset + 0] = static_cast<std::byte>((value >> 24) & 0xff);
  bytes[offset + 1] = static_cast<std::byte>((value >> 16) & 0xff);
  bytes[offset + 2] = static_cast<std::byte>((value >> 8) & 0xff);
  bytes[offset + 3] = static_cast<std::byte>(value & 0xff);
}

void writeU64(std::span<std::byte> bytes, std::size_t offset,
              std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < 8; ++index) {
    const auto shift = static_cast<unsigned>((7 - index) * 8);
    bytes[offset + index] = static_cast<std::byte>((value >> shift) & 0xff);
  }
}

}  // namespace Rudp::Utils
