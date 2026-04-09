#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "Rudp/Protocol.hpp"

namespace Rudp::Utils {

[[nodiscard]] std::uint32_t readU32(std::span<const std::byte> bytes,
                                    std::size_t offset) noexcept;

[[nodiscard]] std::uint64_t readU64(std::span<const std::byte> bytes,
                                    std::size_t offset) noexcept;

void writeU32(std::span<std::byte> bytes, std::size_t offset,
              std::uint32_t value) noexcept;

void writeU64(std::span<std::byte> bytes, std::size_t offset,
              std::uint64_t value) noexcept;

[[nodiscard]] std::string trim(std::string_view value);

[[nodiscard]] std::string unquote(std::string_view value);

[[nodiscard]] bool parseBool(std::string_view value, bool& target) noexcept;

[[nodiscard]] std::string channelTypeName(Rudp::ChannelType type);

}  // namespace Rudp::Utils
