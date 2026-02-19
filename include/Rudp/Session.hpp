#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "Rudp/Protocol.hpp"

namespace Rudp {

enum class ConnState : uint8_t {
    Closed,
    SynSent,
    SynReceived,
    Established,
    TimedOut
};

enum class TxKind : uint8_t {
    None,
    Ack,      // payload_len = 0
    Ctrl      // control plane packet
};

struct TxData {
    TxKind Kind{TxKind::None};
    Protocol::ControlType Ctrl{}; // valid only if Kind == Ctrl
};

struct SessionConfig {
    uint32_t HandshakeTimeoutMs{1500};
    uint32_t IdleTimeoutMs{5000};
    uint32_t HeartbeatIntervalMs{500};
};

class Session {
public:
    explicit Session(bool isServer, SessionConfig cfg);

    ConnState State() const noexcept { return state_; }
    uint32_t  ConnId() const noexcept { return connId_; }

    // client only
    std::optional<TxData> AppConnect(uint32_t nowMs);

    // network receive
    std::optional<TxData> OnRx(uint32_t nowMs,
                               const Protocol::HeaderV1& hdr,
                               std::span<const uint8_t> payload);

    // timer tick
    std::optional<TxData> OnTick(uint32_t nowMs);

private:
    bool isServer_;
    SessionConfig cfg_;

    ConnState state_{ConnState::Closed};
    uint32_t  connId_{0};

    uint32_t lastRxMs_{0};
    uint32_t lastTxMs_{0};

    uint32_t synStartMs_{0};

private:
    bool IsControl(const Protocol::HeaderV1& hdr) const noexcept;
    std::optional<Protocol::ControlType>
        ParseControl(std::span<const uint8_t> payload) const noexcept;

    uint32_t AllocateConnId() const;
};

} // namespace Rudp
