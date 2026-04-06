#pragma once

#include <optional>
#include <span>
#include <vector>

#include "Rudp/Protocol.hpp"

namespace Rudp::Codec {

[[nodiscard]] bool isValidHeader(const Header& header) noexcept;

[[nodiscard]] std::optional<PacketView> decode(std::span<const std::byte> bytes) noexcept;

[[nodiscard]] std::vector<std::byte> encode(const Header& header,
                                            std::span<const std::byte> payload);

}  // namespace Rudp::Codec
