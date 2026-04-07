#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Rudp/ServerSessionManager.hpp"

namespace Rudp::Runtime {

struct ReceivedDatagram final {
  Session::EndpointKey endpoint;
  std::vector<std::byte> bytes;
};

class BsdUdpSocket final {
 public:
  BsdUdpSocket() = default;
  ~BsdUdpSocket();

  BsdUdpSocket(const BsdUdpSocket&) = delete;
  BsdUdpSocket& operator=(const BsdUdpSocket&) = delete;

  BsdUdpSocket(BsdUdpSocket&& other) noexcept;
  BsdUdpSocket& operator=(BsdUdpSocket&& other) noexcept;

  [[nodiscard]] static std::optional<BsdUdpSocket> create_non_blocking();

  [[nodiscard]] bool bind(std::string_view address, std::uint16_t port);
  [[nodiscard]] bool send_to(const Session::EndpointKey& endpoint,
                             std::span<const std::byte> bytes) const;
  [[nodiscard]] std::optional<ReceivedDatagram> recv_from(
      std::size_t buffer_size) const;
  [[nodiscard]] int native_handle() const noexcept { return fd_; }

 private:
  explicit BsdUdpSocket(int fd) noexcept : fd_(fd) {}
  void close() noexcept;

  int fd_ = -1;
};

}  // namespace Rudp::Runtime
