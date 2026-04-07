#include "Rudp/BsdUdpSocket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

namespace Rudp::Runtime {
namespace {

[[nodiscard]] std::optional<sockaddr_in> make_sockaddr(std::string_view address,
                                                       std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  addrinfo* result = nullptr;
  const auto service = std::to_string(port);
  const int status = ::getaddrinfo(std::string(address).c_str(), service.c_str(),
                                   &hints, &result);
  if (status != 0 || result == nullptr) {
    return std::nullopt;
  }

  sockaddr_in addr =
      *reinterpret_cast<sockaddr_in*>(result->ai_addr);
  ::freeaddrinfo(result);
  return addr;
}

}  // namespace

BsdUdpSocket::~BsdUdpSocket() { close(); }

BsdUdpSocket::BsdUdpSocket(BsdUdpSocket&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

BsdUdpSocket& BsdUdpSocket::operator=(BsdUdpSocket&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

std::optional<BsdUdpSocket> BsdUdpSocket::create_non_blocking() {
  const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    std::perror("socket");
    return std::nullopt;
  }

  const int current_flags = ::fcntl(fd, F_GETFL, 0);
  if (current_flags < 0 ||
      ::fcntl(fd, F_SETFL, current_flags | O_NONBLOCK) < 0) {
    std::perror("fcntl");
    ::close(fd);
    return std::nullopt;
  }

  return BsdUdpSocket(fd);
}

bool BsdUdpSocket::bind(std::string_view address, std::uint16_t port) {
  const auto addr = make_sockaddr(address, port);
  if (!addr.has_value()) {
    std::cerr << "Invalid IPv4 address: " << address << '\n';
    return false;
  }

  const int opt = 1;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&*addr), sizeof(*addr)) <
      0) {
    std::perror("bind");
    return false;
  }
  return true;
}

bool BsdUdpSocket::send_to(const Session::EndpointKey& endpoint,
                           std::span<const std::byte> bytes) const {
  const auto addr = make_sockaddr(endpoint.address, endpoint.port);
  if (!addr.has_value()) {
    std::cerr << "Invalid endpoint address: " << endpoint.address << '\n';
    return false;
  }

  const auto sent = ::sendto(fd_, bytes.data(), bytes.size(), 0,
                             reinterpret_cast<const sockaddr*>(&*addr),
                             sizeof(*addr));
  if (sent < 0) {
    std::perror("sendto");
    return false;
  }
  return true;
}

std::optional<ReceivedDatagram> BsdUdpSocket::recv_from(
    std::size_t buffer_size) const {
  std::vector<std::byte> buffer(buffer_size);
  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);
  const auto received =
      ::recvfrom(fd_, buffer.data(), buffer.size(), 0,
                 reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (received < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return std::nullopt;
    }
    std::perror("recvfrom");
    return std::nullopt;
  }

  char address_buf[INET_ADDRSTRLEN] = {};
  if (::inet_ntop(AF_INET, &addr.sin_addr, address_buf, sizeof(address_buf)) ==
      nullptr) {
    std::perror("inet_ntop");
    return std::nullopt;
  }

  buffer.resize(static_cast<std::size_t>(received));
  return ReceivedDatagram{
      .endpoint =
          Session::EndpointKey{
              .address = address_buf,
              .port = ntohs(addr.sin_port),
          },
      .bytes = std::move(buffer),
  };
}

void BsdUdpSocket::close() noexcept {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

}  // namespace Rudp::Runtime
