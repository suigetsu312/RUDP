#include "Rudp/ReliableRx.hpp"

namespace Rudp {

void ReliableRx::OnRxSeq(uint32_t seq) noexcept {
    if (seq_lt(seq, ack_)) {
        return; // already behind the cumulative frontier
    }

    if (seq == ack_) {
        // Consume the missing packet at Ack.
        ++ack_;

        // If the next sequences (old Ack+1, old Ack+2, ...) were already received,
        // they were recorded in bits_ as bit0, bit1, ...
        // Advancing Ack slides the window by 1 each time.
        bool next_received = (bits_ & 1ULL) != 0ULL;

        // Slide once for the Ack++ we just did (drop old bit0 which corresponded to new Ack).
        bits_ >>= 1;

        while (next_received) {
            ++ack_;
            next_received = (bits_ & 1ULL) != 0ULL;
            bits_ >>= 1;
        }
        return;
    }

    uint32_t delta = seq - ack_;
    if (delta >= 1 && delta <= 64) {
        bits_ |= (1ULL << (delta - 1));
    }
    // else: out-of-window -> ignore in v1.1
}

} // namespace Rudp