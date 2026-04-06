#include "Rudp/Codec.hpp"

#include <algorithm>

#include "Rudp/Utils.hpp"

namespace Rudp::Codec {

bool isValidHeader(const Header& header) noexcept {
  if (header.header_len != kHeaderLength) {
    return false;
  }
  if (header.reserved != 0) {
    return false;
  }
  if (static_cast<std::uint8_t>(header.channel_type) > 3) {
    return false;
  }
  return (header.flags & 0xc0U) == 0;
}

std::optional<PacketView> decode(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < kHeaderLength) {
    return std::nullopt;
  }
  Header header_out;
  header_out.conn_id = Utils::readU32(bytes, 0);
  header_out.seq = Utils::readU32(bytes, 4);
  header_out.ack = Utils::readU32(bytes, 8);
  header_out.ack_bits = Utils::readU64(bytes, 12);
  header_out.channel_id = Utils::readU32(bytes, 20);
  header_out.channel_type =
      static_cast<ChannelType>(std::to_integer<std::uint8_t>(bytes[24]));
  header_out.flags = std::to_integer<Flags>(bytes[25]);
  header_out.header_len = std::to_integer<std::uint8_t>(bytes[26]);
  header_out.reserved = std::to_integer<std::uint8_t>(bytes[27]);

  if (!isValidHeader(header_out)) {
    return std::nullopt;
  }

  return PacketView{
      .header = header_out,
      .payload = bytes.subspan(kHeaderLength),
  };
}

std::vector<std::byte> encode(const Header& header,
                              std::span<const std::byte> payload) {
  std::vector<std::byte> bytes(kHeaderLength + payload.size());
  Utils::writeU32(bytes, 0, header.conn_id);
  Utils::writeU32(bytes, 4, header.seq);
  Utils::writeU32(bytes, 8, header.ack);
  Utils::writeU64(bytes, 12, header.ack_bits);
  Utils::writeU32(bytes, 20, header.channel_id);
  bytes[24] = static_cast<std::byte>(header.channel_type);
  bytes[25] = static_cast<std::byte>(header.flags);
  bytes[26] = static_cast<std::byte>(header.header_len);
  bytes[27] = static_cast<std::byte>(header.reserved);
  std::copy(payload.begin(), payload.end(), bytes.begin() + kHeaderLength);
  return bytes;
}

}  // namespace Rudp::Codec
