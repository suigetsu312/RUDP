#include "Rudp/ServerSessionManager.hpp"

#include <functional>
#include <random>

#include "Rudp/Codec.hpp"
#include "Rudp/ConnectionStateMachine.hpp"

namespace Rudp::Session {
namespace {

[[nodiscard]] std::size_t hash_combine(std::size_t seed,
                                       std::size_t value) noexcept {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

}  // namespace

std::size_t EndpointKeyHash::operator()(const EndpointKey& endpoint) const
    noexcept {
  std::size_t seed = std::hash<std::string>{}(endpoint.address);
  seed = hash_combine(seed, std::hash<std::uint16_t>{}(endpoint.port));
  return seed;
}

void ServerSessionManager::on_datagram_received(const EndpointKey& endpoint,
                                                std::span<const std::byte> bytes,
                                                std::uint64_t now_ms) {
  const auto decoded = Rudp::Codec::decode(bytes);
  if (!decoded.has_value()) {
    return;
  }

  if (route_existing_active(endpoint, bytes, decoded->header, now_ms)) {
    return;
  }

  if (route_existing_pending(endpoint, bytes, now_ms)) {
    return;
  }

  if (!route_new_peer(endpoint, bytes, decoded->header, now_ms)) {
    // A non-zero conn_id claims to belong to an already-known connection. If
    // it did not match an active or pending route, drop it.
    return;
  }
}

std::vector<OutboundDatagram> ServerSessionManager::poll_tx(
    std::uint64_t now_ms) {
  std::vector<OutboundDatagram> outbound;
  std::vector<EndpointKey> pending_to_cleanup;
  std::vector<std::pair<EndpointKey, std::uint32_t>> active_to_cleanup;

  collect_pending_tx(now_ms, outbound, pending_to_cleanup);
  collect_active_tx(now_ms, outbound, active_to_cleanup);

  for (const auto& endpoint : pending_to_cleanup) {
    auto pending_it = find_pending_session(endpoint);
    if (pending_it != pending_by_endpoint_.end()) {
      cleanup_pending_session(endpoint, pending_it);
    }
  }

  for (const auto& [endpoint, conn_id] : active_to_cleanup) {
    cleanup_active_session(endpoint, conn_id);
  }

  return outbound;
}

bool ServerSessionManager::is_terminal_state(ConnectionState state) const
    noexcept {
  return state == ConnectionState::Reset || state == ConnectionState::Closed;
}

bool ServerSessionManager::route_existing_active(
    const EndpointKey& endpoint,
    std::span<const std::byte> bytes,
    const Rudp::Header& header,
    std::uint64_t now_ms) {
  if (header.conn_id == 0) {
    return false;
  }
  if (!has_active_session(header.conn_id)) {
    return false;
  }
  if (!is_active_endpoint_match(endpoint, header.conn_id)) {
    return true;
  }
  return try_dispatch_active(endpoint, bytes, header.conn_id, now_ms);
}

bool ServerSessionManager::is_active_endpoint_match(
    const EndpointKey& endpoint,
    std::uint32_t conn_id) const {
  const auto it = active_conn_id_by_endpoint_.find(endpoint);
  return it != active_conn_id_by_endpoint_.end() && it->second == conn_id;
}

bool ServerSessionManager::route_existing_pending(
    const EndpointKey& endpoint,
    std::span<const std::byte> bytes,
    std::uint64_t now_ms) {
  return try_dispatch_pending(endpoint, bytes, now_ms);
}

bool ServerSessionManager::route_new_peer(const EndpointKey& endpoint,
                                          std::span<const std::byte> bytes,
                                          const Rudp::Header& header,
                                          std::uint64_t now_ms) {
  if (header.conn_id != 0) {
    return false;
  }

  const auto pending_it = ensure_session_for_new_peer(endpoint);
  if (pending_it == pending_by_endpoint_.end()) {
    return false;
  }

  static_cast<void>(try_dispatch_pending(endpoint, bytes, now_ms));
  return true;
}

bool ServerSessionManager::try_dispatch_active(const EndpointKey& endpoint,
                                               std::span<const std::byte> bytes,
                                               std::uint32_t conn_id,
                                               std::uint64_t now_ms) {
  auto active_it = find_active_session(conn_id);
  if (active_it == active_by_conn_id_.end()) {
    return false;
  }

  active_it->second.on_datagram_received(bytes, now_ms);
  cleanup_active_if_terminal(endpoint, conn_id, active_it);
  return true;
}

bool ServerSessionManager::try_dispatch_pending(const EndpointKey& endpoint,
                                                std::span<const std::byte> bytes,
                                                std::uint64_t now_ms) {
  auto pending_it = find_pending_session(endpoint);
  if (pending_it == pending_by_endpoint_.end()) {
    return false;
  }

  pending_it->second.on_datagram_received(bytes, now_ms);
  const auto state = pending_it->second.connection_state();
  if (state == ConnectionState::Established) {
    promote_pending_session(endpoint, pending_it);
  }
  cleanup_pending_if_terminal(endpoint, pending_it);
  return true;
}

void ServerSessionManager::cleanup_pending_if_terminal(
    const EndpointKey& endpoint,
    PendingMap::iterator pending_it) {
  if (pending_it == pending_by_endpoint_.end()) {
    return;
  }
  if (!is_terminal_state(pending_it->second.connection_state())) {
    return;
  }
  cleanup_pending_session(endpoint, pending_it);
}

void ServerSessionManager::cleanup_active_if_terminal(
    const EndpointKey& endpoint,
    std::uint32_t conn_id,
    ActiveMap::iterator active_it) {
  if (active_it == active_by_conn_id_.end()) {
    return;
  }
  if (!is_terminal_state(active_it->second.connection_state())) {
    return;
  }
  cleanup_active_session(endpoint, conn_id);
}

void ServerSessionManager::collect_pending_tx(
    std::uint64_t now_ms,
    std::vector<OutboundDatagram>& outbound,
    std::vector<EndpointKey>& pending_to_cleanup) {
  for (auto& [endpoint, session] : pending_by_endpoint_) {
    auto bytes = session.poll_tx(now_ms);
    if (bytes.has_value()) {
      outbound.push_back(OutboundDatagram{
          .endpoint = endpoint,
          .bytes = std::move(*bytes),
      });
      continue;
    }

    if (is_terminal_state(session.connection_state())) {
      pending_to_cleanup.push_back(endpoint);
    }
  }
}

void ServerSessionManager::collect_active_tx(
    std::uint64_t now_ms,
    std::vector<OutboundDatagram>& outbound,
    std::vector<std::pair<EndpointKey, std::uint32_t>>& active_to_cleanup) {
  for (const auto& [endpoint, conn_id] : active_conn_id_by_endpoint_) {
    auto active_it = find_active_session(conn_id);
    if (active_it == active_by_conn_id_.end()) {
      continue;
    }

    auto bytes = active_it->second.poll_tx(now_ms);
    if (bytes.has_value()) {
      outbound.push_back(OutboundDatagram{
          .endpoint = endpoint,
          .bytes = std::move(*bytes),
      });
      continue;
    }

    if (is_terminal_state(active_it->second.connection_state())) {
      active_to_cleanup.push_back({endpoint, conn_id});
    }
  }
}

ServerSessionManager::PendingMap::iterator
ServerSessionManager::find_pending_session(const EndpointKey& endpoint) {
  return pending_by_endpoint_.find(endpoint);
}

ServerSessionManager::PendingMap::const_iterator
ServerSessionManager::find_pending_session(const EndpointKey& endpoint) const {
  return pending_by_endpoint_.find(endpoint);
}

bool ServerSessionManager::has_pending_session(
    const EndpointKey& endpoint) const noexcept {
  return find_pending_session(endpoint) != pending_by_endpoint_.end();
}

bool ServerSessionManager::has_active_session(std::uint32_t conn_id) const
    noexcept {
  return find_active_session(conn_id) != active_by_conn_id_.end();
}

std::optional<std::uint32_t> ServerSessionManager::pending_conn_id(
    const EndpointKey& endpoint) const noexcept {
  const auto it = find_pending_session(endpoint);
  if (it == pending_by_endpoint_.end()) {
    return std::nullopt;
  }
  return it->second.conn_id();
}

std::optional<std::uint32_t> ServerSessionManager::active_conn_id(
    const EndpointKey& endpoint) const noexcept {
  const auto it = active_conn_id_by_endpoint_.find(endpoint);
  if (it == active_conn_id_by_endpoint_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<ConnectionState> ServerSessionManager::pending_connection_state(
    const EndpointKey& endpoint) const noexcept {
  const auto it = find_pending_session(endpoint);
  if (it == pending_by_endpoint_.end()) {
    return std::nullopt;
  }
  return it->second.connection_state();
}

std::optional<SessionStats> ServerSessionManager::active_stats(
    std::uint32_t conn_id) const noexcept {
  const auto it = find_active_session(conn_id);
  if (it == active_by_conn_id_.end()) {
    return std::nullopt;
  }
  return it->second.stats();
}

std::vector<SessionEvent> ServerSessionManager::drain_active_events(
    std::uint32_t conn_id) {
  const auto it = find_active_session(conn_id);
  if (it == active_by_conn_id_.end()) {
    return {};
  }
  return it->second.drain_events();
}

bool ServerSessionManager::queue_send(std::uint32_t conn_id,
                                      std::uint32_t channel_id,
                                      Rudp::ChannelType channel_type,
                                      std::span<const std::byte> payload) {
  const auto it = find_active_session(conn_id);
  if (it == active_by_conn_id_.end()) {
    return false;
  }

  it->second.queue_send(channel_id, channel_type, payload);
  return true;
}

std::vector<ServerSessionEvent> ServerSessionManager::drain_events() {
  std::vector<ServerSessionEvent> events;

  for (auto& [endpoint, session] : pending_by_endpoint_) {
    auto drained = session.drain_events();
    for (auto& event : drained) {
      events.push_back(ServerSessionEvent{
          .endpoint = endpoint,
          .conn_id = session.conn_id() != 0
                         ? std::optional<std::uint32_t>(session.conn_id())
                         : std::nullopt,
          .event = std::move(event),
      });
    }
  }

  for (const auto& [endpoint, conn_id] : active_conn_id_by_endpoint_) {
    auto it = find_active_session(conn_id);
    if (it == active_by_conn_id_.end()) {
      continue;
    }

    auto drained = it->second.drain_events();
    for (auto& event : drained) {
      events.push_back(ServerSessionEvent{
          .endpoint = endpoint,
          .conn_id = conn_id,
          .event = std::move(event),
      });
    }
  }

  return events;
}

ServerSessionManager::ActiveMap::iterator
ServerSessionManager::find_active_session(std::uint32_t conn_id) {
  return active_by_conn_id_.find(conn_id);
}

ServerSessionManager::ActiveMap::const_iterator
ServerSessionManager::find_active_session(std::uint32_t conn_id) const {
  return active_by_conn_id_.find(conn_id);
}

ServerSessionManager::PendingMap::iterator
ServerSessionManager::ensure_session_for_new_peer(const EndpointKey& endpoint) {
  auto pending_it = find_pending_session(endpoint);
  if (pending_it != pending_by_endpoint_.end()) {
    return pending_it;
  }

  if (active_conn_id_by_endpoint_.find(endpoint) !=
      active_conn_id_by_endpoint_.end()) {
    return pending_by_endpoint_.end();
  }

  pending_it =
      pending_by_endpoint_.try_emplace(endpoint, SessionRole::Server).first;
  pending_it->second.assign_conn_id(allocate_conn_id());
  return pending_it;
}

void ServerSessionManager::promote_pending_session(
    const EndpointKey& endpoint,
    PendingMap::iterator pending_it) {
  if (pending_it == pending_by_endpoint_.end()) {
    return;
  }

  const auto conn_id = pending_it->second.conn_id();
  if (conn_id == 0) {
    return;
  }

  active_conn_id_by_endpoint_[endpoint] = conn_id;
  active_by_conn_id_.insert_or_assign(conn_id, std::move(pending_it->second));
  pending_by_endpoint_.erase(pending_it);
}

void ServerSessionManager::cleanup_pending_session(
    const EndpointKey& endpoint,
    PendingMap::iterator pending_it) {
  (void)endpoint;
  if (pending_it == pending_by_endpoint_.end()) {
    return;
  }
  const auto conn_id = pending_it->second.conn_id();
  if (conn_id != 0) {
    retired_conn_ids_.insert(conn_id);
  }
  pending_by_endpoint_.erase(pending_it);
}

void ServerSessionManager::cleanup_active_session(const EndpointKey& endpoint,
                                                  std::uint32_t conn_id) {
  if (conn_id != 0) {
    retired_conn_ids_.insert(conn_id);
  }
  active_by_conn_id_.erase(conn_id);
  const auto endpoint_it = active_conn_id_by_endpoint_.find(endpoint);
  if (endpoint_it != active_conn_id_by_endpoint_.end() &&
      endpoint_it->second == conn_id) {
    active_conn_id_by_endpoint_.erase(endpoint_it);
  }
}

bool ServerSessionManager::conn_id_is_in_use(std::uint32_t conn_id) const
    noexcept {
  if (conn_id == 0) {
    return true;
  }

  if (retired_conn_ids_.contains(conn_id)) {
    return true;
  }

  if (active_by_conn_id_.find(conn_id) != active_by_conn_id_.end()) {
    return true;
  }

  for (const auto& [endpoint, session] : pending_by_endpoint_) {
    (void)endpoint;
    if (session.conn_id() == conn_id) {
      return true;
    }
  }

  return false;
}

std::uint32_t ServerSessionManager::allocate_conn_id() {
  std::random_device rd;

  std::uint32_t value = 0;
  do {
    const auto upper = static_cast<std::uint32_t>(rd()) << 16U;
    const auto lower = static_cast<std::uint32_t>(rd()) & 0xffffU;
    value = upper ^ lower;
  } while (conn_id_is_in_use(value));

  return value;
}

}  // namespace Rudp::Session
