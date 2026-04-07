#pragma once

#include <vector>

#include "Rudp/ConnectionStateMachine.hpp"
#include "Rudp/Protocol.hpp"
#include "Rudp/SessionTypes.hpp"

namespace Rudp::Session {

class RxHandler final {
 public:
  [[nodiscard]] RxPacketResult on_packet(const Rudp::PacketView& packet,
                                         std::uint64_t now_ms,
                                         ControlKind control_kind,
                                         RxSessionState& rx);

  [[nodiscard]] std::vector<SessionEvent> drain_events(RxSessionState& rx);

 private:
  [[nodiscard]] bool update_reliable_receive_state(
      const Rudp::PacketView& packet,
      ControlKind control_kind,
      RxPacketResult& result,
      RxSessionState& rx);

  void handle_reliable_ordered(const Rudp::PacketView& packet,
                               RxSessionState& rx);

  void handle_reliable_unordered(const Rudp::PacketView& packet,
                                 RxSessionState& rx);

  void handle_unreliable(const Rudp::PacketView& packet, RxSessionState& rx);

  void handle_monotonic_state(const Rudp::PacketView& packet,
                              RxSessionState& rx);
};

}  // namespace Rudp::Session
