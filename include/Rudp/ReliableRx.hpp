#pragma once
#include <cstdint>

namespace Rudp {

/// Connection-level reliable receive state (per-direction).
///
/// ACK model:
///   Ack = next expected reliable sequence number (rcv_nxt)
///   AckBits (forward mask):
///     bit i corresponds to seq = Ack + i + 1, for i in [0,63]
///     bit=1 means that seq has been received (out-of-order)
///
/// Notes:
/// - Track ONLY the reliable sequence space.
/// - Unreliable packets must not call OnRxSeq().
class ReliableRx {
public:
    explicit ReliableRx(uint32_t initial_ack = 0) noexcept
        : ack_(initial_ack), bits_(0) {}

    void OnRxSeq(uint32_t seq) noexcept;

    uint32_t Ack() const noexcept { return ack_; }
    uint64_t AckBits() const noexcept { return bits_; }

    void Reset(uint32_t initial_ack) noexcept {
        ack_ = initial_ack;
        bits_ = 0;
    }

private:
    static constexpr bool seq_lt(uint32_t a, uint32_t b) noexcept {
        return static_cast<int32_t>(a - b) < 0;
    }

private:
    uint32_t ack_;   // next expected
    uint64_t bits_;  // bit i => ack_ + i + 1 received
};

} // namespace Rudp