#ifndef SOCKET_TOOLS_HPP
#define SOCKET_TOOLS_HPP
#include <unordered_map>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif


struct sockaddr_in_hash {
    std::size_t operator()(const sockaddr_in& addr) const {
        return std::hash<uint32_t>()(addr.sin_addr.s_addr) ^ std::hash<uint16_t>()(addr.sin_port);
    }
};

struct sockaddr_in_equal {
    bool operator()(const sockaddr_in& lhs, const sockaddr_in& rhs) const {
        return lhs.sin_addr.s_addr == rhs.sin_addr.s_addr && lhs.sin_port == rhs.sin_port;
    }
};



#endif