#include "Rudp/Session.hpp"
#include <random>
#include <atomic>
namespace {

    constexpr uint32_t kMinConnId = 1;
    
    uint32_t MakeRandomBase(){
        std::random_device rd;
        uint32_t a = static_cast<uint32_t>(rd());
        uint32_t b = static_cast<uint32_t>(rd());
        uint32_t x = (a<< 16) ^ b;

        if (x< kMinConnId) x = kMinConnId;
        return x;
    }

    #ifdef RUDP_DEBUG_DETERMINISTIC_CONNID
    static std::atomic<uint32_t> g_nextConnId{kMinConnId};
    #else
    static std::atomic<uint32_t> g_nextConnId{MakeRandomBase()};
    #endif

    uint32_t NextConnId(){
        uint32_t id = g_nextConnId.fetch_add(1, std::memory_order_relaxed);
        if (id == 0){
            // extremely unlikely due to init, but handle wrap-around.
            id = g_nextConnId.fetch_add(1, std::memory_order_relaxed);
            if (id == 0) id = kMinConnId; 
        }

        return id;
    };
};

namespace Rudp {

enum class RxEvent : uint8_t {
    CtrlSyn,
    CtrlSynAck,
    CtrlReset,
    CtrlFin,
    DataOrAckOnly,
    Unknown
};

static RxEvent ClassifyRx(const Protocol::HeaderV1& h, std::span<const uint8_t> p) {
    if (h.ChannelId != Protocol::ChannelControl) return RxEvent::DataOrAckOnly;
    if (p.size() < 1) return RxEvent::Unknown;

    auto ct = static_cast<Protocol::ControlType>(p[0]);
    switch (ct) {
        case Protocol::ControlType::Syn:    return RxEvent::CtrlSyn;
        case Protocol::ControlType::SynAck: return RxEvent::CtrlSynAck;
        case Protocol::ControlType::Reset:  return RxEvent::CtrlReset;
        case Protocol::ControlType::Fin:    return RxEvent::CtrlFin;
        default: return RxEvent::Unknown;
    }
}

Session::Session(bool isServer, SessionConfig cfg)
    : isServer_(isServer), cfg_(cfg) {}

bool Session::IsControl(const Protocol::HeaderV1& hdr) const noexcept {
    return hdr.ChannelId == Protocol::ChannelControl;
}

std::optional<Protocol::ControlType>
Session::ParseControl(std::span<const uint8_t> payload) const noexcept {
    if (payload.size() < 1) return std::nullopt;
    return static_cast<Protocol::ControlType>(payload[0]);
}

uint32_t Session::AllocateConnId() const {
    return NextConnId();
}

std::optional<TxData> Session::AppConnect(uint32_t nowMs) {
    if (isServer_) return std::nullopt;
    if (state_ != ConnState::Closed) return std::nullopt;

    state_ = ConnState::SynSent;
    connId_ = 0;
    synStartMs_ = nowMs;
    lastTxMs_ = nowMs;

    return TxData{ .Kind = TxKind::Ctrl, .Ctrl = Protocol::ControlType::Syn };
}

std::optional<TxData> Session::OnRx(uint32_t nowMs,
                                   const Protocol::HeaderV1& hdr,
                                   std::span<const uint8_t> payload) {
    lastRxMs_ = nowMs;

    const RxEvent ev = ClassifyRx(hdr, payload);

    // Global rule: RESET always wins
    if (ev == RxEvent::CtrlReset) {
        state_ = ConnState::Closed;
        connId_ = 0;
        return std::nullopt;
    }

    switch (state_) {
    case ConnState::Closed:
        if (isServer_) {
            if (ev == RxEvent::CtrlSyn && hdr.ConnId == 0) {
                connId_ = AllocateConnId();
                state_ = ConnState::SynReceived;
                synStartMs_ = nowMs;
                lastTxMs_ = nowMs;
                return TxData{TxKind::Ctrl, Protocol::ControlType::SynAck};
            }
        }
        return std::nullopt;

    case ConnState::SynSent:
        if (!isServer_) {
            if (ev == RxEvent::CtrlSynAck) {
                connId_ = hdr.ConnId;      // accept server assigned connId
                state_ = ConnState::Established;
                lastTxMs_ = nowMs;
                return TxData{TxKind::Ack, Protocol::ControlType{}}; // Ctrl ignored
            }
        }
        return std::nullopt;

    case ConnState::SynReceived:
        if (isServer_) {
            // Final step: any packet that carries the allocated connId confirms the handshake
            if (hdr.ConnId == connId_) {
                state_ = ConnState::Established;
            }
        }
        return std::nullopt;

    case ConnState::Established:
        // v1: no special rx behavior yet (later: handle FIN, data, etc.)
        return std::nullopt;

    case ConnState::TimedOut:
        return std::nullopt;
    }

    return std::nullopt;
}

std::optional<TxData> Session::OnTick(uint32_t nowMs) {
    auto elapsed = [](uint32_t now, uint32_t then) -> uint32_t {
        return (now >= then) ? (now - then) : 0; // v1: assume monotonic; simple guard
    };
    switch (state_) {
        case ConnState::Closed:
            return std::nullopt;
        case ConnState::SynSent:
            // client handshake timeout
            if (elapsed(nowMs, synStartMs_) >= cfg_.HandshakeTimeoutMs) {
                state_ = ConnState::TimedOut;
            }
            return std::nullopt;

        case ConnState::SynReceived:
            // server handshake timeout (drop half-open)
            if (elapsed(nowMs, synStartMs_) >= cfg_.HandshakeTimeoutMs) {
                state_ = ConnState::Closed;
                connId_ = 0;
            }
            return std::nullopt;
        case ConnState::Established:{
            // idle timeout: no inbound traffic for too long
            if (lastRxMs_ != 0 && elapsed(nowMs, lastRxMs_) >= cfg_.IdleTimeoutMs) {
                state_ = ConnState::TimedOut;
                return std::nullopt;
            }

            // heartbeat: emit ack-only if we haven't sent anything for a while
            if (lastTxMs_ == 0 || elapsed(nowMs, lastTxMs_) >= cfg_.HeartbeatIntervalMs) {
                lastTxMs_ = nowMs;
                return TxData{ .Kind = TxKind::Ack, .Ctrl = Protocol::ControlType{} };
            }

            return std::nullopt;
        }

        case ConnState::TimedOut:
            return std::nullopt;
    }
    return std::nullopt;
}

} // namespace Rudp
