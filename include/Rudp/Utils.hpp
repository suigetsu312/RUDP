#ifndef RUDP_UTILS_HPP
#define RUDP_UTILS_HPP
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

namespace Rudp::Utils {

template <class T>
requires (std::is_integral_v<T> && std::is_unsigned_v<T>)
constexpr T ToBigEndian(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) return value;
    return std::byteswap(value);
}

template <class T>
requires (std::is_integral_v<T> && std::is_unsigned_v<T>)
constexpr T FromBigEndian(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) return value;
    return std::byteswap(value);
}

inline bool WriteU16BE(std::span<uint8_t> out, size_t offset, uint16_t value) noexcept {
    if (offset + 2 > out.size()) return false;
    value = ToBigEndian(value);
    std::memcpy(out.data() + offset, &value, 2);
    return true;
}

inline bool WriteU32BE(std::span<uint8_t> out, size_t offset, uint32_t value) noexcept {
    if (offset + 4 > out.size()) return false;
    value = ToBigEndian(value);
    std::memcpy(out.data() + offset, &value, 4);
    return true;
}

inline bool WriteU64BE(std::span<uint8_t> out, size_t offset, uint64_t value) noexcept {
    if (offset + 8 > out.size()) return false;
    value = ToBigEndian(value);
    std::memcpy(out.data() + offset, &value, 8);
    return true;
}

inline bool ReadU16BE(std::span<const uint8_t> in, size_t offset, uint16_t& value) noexcept {
    if (offset + 2 > in.size()) return false;
    std::memcpy(&value, in.data() + offset, 2);
    value = FromBigEndian(value);
    return true;
}

inline bool ReadU32BE(std::span<const uint8_t> in, size_t offset, uint32_t& value) noexcept {
    if (offset + 4 > in.size()) return false;
    std::memcpy(&value, in.data() + offset, 4);
    value = FromBigEndian(value);
    return true;
}

inline bool ReadU64BE(std::span<const uint8_t> in, size_t offset, uint64_t& value) noexcept {
    if (offset + 8 > in.size()) return false;
    std::memcpy(&value, in.data() + offset, 8);
    value = FromBigEndian(value);
    return true;
}

inline bool IsSeqNewer(uint32_t a, uint32_t b) noexcept {
    return static_cast<int32_t>(a - b) > 0;
}

} // namespace Rudp::Utils

#endif