#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Rudp/Session.hpp"

namespace Rudp::Session {

struct EndpointKey final {
  std::string address;
  std::uint16_t port = 0;

  [[nodiscard]] bool operator==(const EndpointKey& other) const noexcept =
      default;
};

struct EndpointKeyHash final {
  [[nodiscard]] std::size_t operator()(const EndpointKey& endpoint) const
      noexcept;
};

struct OutboundDatagram final {
  EndpointKey endpoint;
  std::vector<std::byte> bytes;
};

class ServerSessionManager final {
 public:
  void on_datagram_received(const EndpointKey& endpoint,
                            std::span<const std::byte> bytes,
                            std::uint64_t now_ms);

  [[nodiscard]] std::vector<OutboundDatagram> poll_tx(std::uint64_t now_ms);

  [[nodiscard]] std::size_t pending_session_count() const noexcept {
    return pending_by_endpoint_.size();
  }

  [[nodiscard]] std::size_t active_session_count() const noexcept {
    return active_by_conn_id_.size();
  }

  [[nodiscard]] bool has_pending_session(const EndpointKey& endpoint) const
      noexcept;
  [[nodiscard]] bool has_active_session(std::uint32_t conn_id) const noexcept;

  [[nodiscard]] std::optional<std::uint32_t> pending_conn_id(
      const EndpointKey& endpoint) const noexcept;
  [[nodiscard]] std::optional<std::uint32_t> active_conn_id(
      const EndpointKey& endpoint) const noexcept;

  [[nodiscard]] std::optional<ConnectionState> pending_connection_state(
      const EndpointKey& endpoint) const noexcept;

  [[nodiscard]] std::vector<SessionEvent> drain_active_events(
      std::uint32_t conn_id);

 private:
  using PendingMap =
      std::unordered_map<EndpointKey, Session, EndpointKeyHash>;
  using ActiveMap = std::unordered_map<std::uint32_t, Session>;
  using EndpointToConnIdMap =
      std::unordered_map<EndpointKey, std::uint32_t, EndpointKeyHash>;

  [[nodiscard]] PendingMap::iterator find_pending_session(
      const EndpointKey& endpoint);
  [[nodiscard]] PendingMap::const_iterator find_pending_session(
      const EndpointKey& endpoint) const;

  [[nodiscard]] ActiveMap::iterator find_active_session(std::uint32_t conn_id);
  [[nodiscard]] ActiveMap::const_iterator find_active_session(
      std::uint32_t conn_id) const;

  [[nodiscard]] PendingMap::iterator ensure_session_for_new_peer(
      const EndpointKey& endpoint);
  [[nodiscard]] bool is_terminal_state(ConnectionState state) const noexcept;
  [[nodiscard]] bool try_dispatch_active(const EndpointKey& endpoint,
                                         std::span<const std::byte> bytes,
                                         std::uint32_t conn_id,
                                         std::uint64_t now_ms);
  [[nodiscard]] bool try_dispatch_pending(const EndpointKey& endpoint,
                                          std::span<const std::byte> bytes,
                                          std::uint64_t now_ms);
  [[nodiscard]] bool route_existing_active(
      const EndpointKey& endpoint,
      std::span<const std::byte> bytes,
      const Rudp::Header& header,
      std::uint64_t now_ms);
  [[nodiscard]] bool route_existing_pending(const EndpointKey& endpoint,
                                            std::span<const std::byte> bytes,
                                            std::uint64_t now_ms);
  [[nodiscard]] bool route_new_peer(const EndpointKey& endpoint,
                                    std::span<const std::byte> bytes,
                                    const Rudp::Header& header,
                                    std::uint64_t now_ms);
  void cleanup_pending_if_terminal(const EndpointKey& endpoint,
                                   PendingMap::iterator pending_it);
  void cleanup_active_if_terminal(const EndpointKey& endpoint,
                                  std::uint32_t conn_id,
                                  ActiveMap::iterator active_it);
  void collect_pending_tx(std::uint64_t now_ms,
                          std::vector<OutboundDatagram>& outbound,
                          std::vector<EndpointKey>& pending_to_cleanup);
  void collect_active_tx(
      std::uint64_t now_ms,
      std::vector<OutboundDatagram>& outbound,
      std::vector<std::pair<EndpointKey, std::uint32_t>>& active_to_cleanup);
  void promote_pending_session(const EndpointKey& endpoint,
                               PendingMap::iterator pending_it);
  void cleanup_pending_session(const EndpointKey& endpoint,
                               PendingMap::iterator pending_it);
  void cleanup_active_session(const EndpointKey& endpoint,
                              std::uint32_t conn_id);
  [[nodiscard]] bool conn_id_is_in_use(std::uint32_t conn_id) const noexcept;
  [[nodiscard]] std::uint32_t allocate_conn_id();

  PendingMap pending_by_endpoint_;
  ActiveMap active_by_conn_id_;
  EndpointToConnIdMap active_conn_id_by_endpoint_;
};

}  // namespace Rudp::Session
