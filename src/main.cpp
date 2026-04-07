#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "Rudp/Protocol.hpp"
#include "Rudp/SessionTypes.hpp"
#include "Rudp/TxHandler.hpp"

namespace {

using Rudp::Session::OwnedPacket;
using Rudp::Session::TxEntry;
using Rudp::Session::TxSessionState;

void print_ack_bits(std::uint64_t ack_bits) {
  std::cout << "0b";
  for (int i = static_cast<int>(Rudp::kAckBitsWindow) - 1; i >= 0; --i) {
    const auto mask = (1ULL << static_cast<unsigned>(i));
    std::cout << ((ack_bits & mask) != 0ULL ? '1' : '0');
  }
}

void print_inflight(const TxSessionState& tx) {
  if (tx.inflight.empty()) {
    std::cout << "  inflight: <empty>\n";
    return;
  }

  std::cout << "  inflight:\n";
  for (const auto& [seq, entry] : tx.inflight) {
    std::cout << "    seq=" << seq
              << " retry_count=" << entry.retry_count
              << " fast_retx_pending="
              << (entry.fast_retx_pending ? "true" : "false") << '\n';
  }
}

void seed_inflight(TxSessionState& tx,
                   std::initializer_list<std::uint32_t> seqs) {
  for (const auto seq : seqs) {
    TxEntry entry;
    entry.packet = OwnedPacket{
        .header =
            Rudp::Header{
                .seq = seq,
                .channel_id = 1,
                .channel_type = Rudp::ChannelType::ReliableUnordered,
            },
        .payload = std::vector<std::byte>{std::byte{0x41}, std::byte{0x42}},
    };
    tx.inflight.emplace(seq, std::move(entry));
  }
}

void run_remote_ack_demo(std::uint32_t ack,
                         std::uint64_t ack_bits,
                         TxSessionState& tx,
                         Rudp::Session::TxHandler& handler) {
  std::cout << "\n== on_remote_ack demo ==\n";
  std::cout << "remote ack=" << ack << " ack_bits=";
  print_ack_bits(ack_bits);
  std::cout << '\n';

  std::cout << "before on_remote_ack:\n";
  print_inflight(tx);

  static_cast<void>(handler.on_remote_ack(ack, ack_bits, tx));

  std::cout << "after on_remote_ack:\n";
  print_inflight(tx);
  std::cout << "  tx.remote_ack=" << tx.remote_ack << " tx.remote_ack_bits=";
  print_ack_bits(tx.remote_ack_bits);
  std::cout << "\n";
}

}  // namespace

int main() {
  Rudp::Session::TxHandler handler;
  TxSessionState tx;

  seed_inflight(tx, {100, 101, 102, 103, 104, 105});

  std::cout << "RUDP TxHandler on_remote_ack demo\n";
  std::cout << "Scenario 1: remote cumulative ACK reaches 103, so packets older "
               "than 103 are removed.\n";
  run_remote_ack_demo(103, 0ULL, tx, handler);

  std::cout << "\nScenario 2: remote ACK front is 104 and selective ACK bits "
               "say 106 and 107 arrived.\n";
  std::cout << "Current code removes acknowledged inflight packets and marks "
               "gap candidates for fast retransmit.\n";
  seed_inflight(tx, {106, 107});
  run_remote_ack_demo(104, (1ULL << 1U) | (1ULL << 2U), tx, handler);

  return 0;
}
