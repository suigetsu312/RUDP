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

  if (decoded->header.conn_id != 0) {
    auto active_it = find_active_session(decoded->header.conn_id);
    if (active_it != active_by_conn_id_.end()) {
      active_it->second.on_datagram_received(bytes, now_ms);
      if (active_it->second.connection_state() == ConnectionState::Reset ||
          active_it->second.connection_state() == ConnectionState::Closed) {
        cleanup_active_session(endpoint, decoded->header.conn_id);
      }
      return;
    }
  }

  auto pending_it = find_pending_session(endpoint);
  if (decoded->header.conn_id == 0 &&
      pending_it == pending_by_endpoint_.end() &&
      active_conn_id_by_endpoint_.find(endpoint) ==
          active_conn_id_by_endpoint_.end()) {
    pending_it = ensure_session_for_new_peer(endpoint);
  }

  if (pending_it != pending_by_endpoint_.end()) {
    pending_it->second.on_datagram_received(bytes, now_ms);
    if (pending_it->second.connection_state() == ConnectionState::Established) {
      promote_pending_session(endpoint, pending_it);
    } else if (pending_it->second.connection_state() == ConnectionState::Reset ||
               pending_it->second.connection_state() ==
                   ConnectionState::Closed) {
      cleanup_pending_session(endpoint, pending_it);
    }
    return;
  }

  if (decoded->header.conn_id != 0) {
    // A non-zero conn_id claims to belong to an already-known connection. If
    // it did not match an active session and it also does not belong to a
    // pending endpoint, drop it.
    return;
  }

  // Skeleton only: remaining conn_id==0 routing rules will move here once the
  // non-established peer policy is finalized.
}

std::vector<OutboundDatagram> ServerSessionManager::poll_tx(
    std::uint64_t now_ms) {
  std::vector<OutboundDatagram> outbound;
  std::vector<EndpointKey> pending_to_cleanup;
  std::vector<std::pair<EndpointKey, std::uint32_t>> active_to_cleanup;

  for (auto& [endpoint, session] : pending_by_endpoint_) {
    auto bytes = session.poll_tx(now_ms);
    if (!bytes.has_value()) {
      if (session.connection_state() == ConnectionState::Reset ||
          session.connection_state() == ConnectionState::Closed) {
        pending_to_cleanup.push_back(endpoint);
      }
    } else {
      outbound.push_back(OutboundDatagram{
          .endpoint = endpoint,
          .bytes = std::move(*bytes),
      });
    }
  }

  for (const auto& [endpoint, conn_id] : active_conn_id_by_endpoint_) {
    auto active_it = find_active_session(conn_id);
    if (active_it == active_by_conn_id_.end()) {
      continue;
    }

    auto bytes = active_it->second.poll_tx(now_ms);
    if (!bytes.has_value()) {
      if (active_it->second.connection_state() == ConnectionState::Reset ||
          active_it->second.connection_state() == ConnectionState::Closed) {
        active_to_cleanup.push_back({endpoint, conn_id});
      }
    } else {
      outbound.push_back(OutboundDatagram{
          .endpoint = endpoint,
          .bytes = std::move(*bytes),
      });
    }
  }

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

std::vector<SessionEvent> ServerSessionManager::drain_active_events(
    std::uint32_t conn_id) {
  const auto it = find_active_session(conn_id);
  if (it == active_by_conn_id_.end()) {
    return {};
  }
  return it->second.drain_events();
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
  pending_by_endpoint_.erase(pending_it);
}

void ServerSessionManager::cleanup_active_session(const EndpointKey& endpoint,
                                                  std::uint32_t conn_id) {
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
