#include "Rudp/Utils.hpp"

#include <cctype>

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

std::string trim(std::string_view value) {
  std::size_t first = 0;
  while (first < value.size() &&
         std::isspace(static_cast<unsigned char>(value[first])) != 0) {
    ++first;
  }

  std::size_t last = value.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(value[last - 1U])) != 0) {
    --last;
  }

  return std::string(value.substr(first, last - first));
}

std::string unquote(std::string_view value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return std::string(value.substr(1, value.size() - 2));
  }
  return std::string(value);
}

bool parseBool(std::string_view value, bool& target) noexcept {
  if (value == "true") {
    target = true;
    return true;
  }
  if (value == "false") {
    target = false;
    return true;
  }
  return false;
}

std::string channelTypeName(Rudp::ChannelType type) {
  switch (type) {
    case Rudp::ChannelType::ReliableOrdered:
      return "reliable_ordered";
    case Rudp::ChannelType::ReliableUnordered:
      return "reliable_unordered";
    case Rudp::ChannelType::Unreliable:
      return "unreliable";
    case Rudp::ChannelType::MonotonicState:
      return "monotonic_state";
  }
  return "unknown";
}

}  // namespace Rudp::Utils
