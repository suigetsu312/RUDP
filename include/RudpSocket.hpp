#ifndef RUDP_SOCKET_HPP
#define RUDP_SOCKET_HPP

#include "protocol.hpp"
#include "PacketHelper.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cstring>  // for memset
#include <thread>
#include <atomic>
#include <chrono>

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


struct IRudpPlugin {
    virtual void on_receive(const packet& pkt, const sockaddr_in& client_addr) = 0;
    virtual void on_send(const packet& pkt, const sockaddr_in& client_addr) = 0;
    virtual ~IRudpPlugin() = default;
};

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

class UdpSocketBase {
protected:
    SOCKET sock = INVALID_SOCKET;

public:
    virtual ~UdpSocketBase() {
        close_socket();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool open_socket() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }
#endif
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == INVALID_SOCKET) {
            std::cerr << "Failed to create socket\n";
            return false;
        }
        return true;
    }

    void close_socket() {
        if (sock != INVALID_SOCKET) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            sock = INVALID_SOCKET;
        }
    }

    bool send_to(const sockaddr_in& addr, const uint8_t* data, size_t len) {
        int ret = sendto(sock, reinterpret_cast<const char*>(data), (int)len, 0,
                         reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        return ret == (int)len;
    }
};

// ------ UDP Server --------

class UdpServer : public UdpSocketBase {
    std::vector<std::shared_ptr<IRudpPlugin>> plugins;

    std::atomic<bool> running{false};
    std::thread recv_thread;

    sockaddr_in server_addr{};
    std::unordered_map<sockaddr_in, uint64_t, sockaddr_in_hash, sockaddr_in_equal> clients;

public:
    UdpServer() = default;

    ~UdpServer() {
        stop();
    }

    bool start(uint16_t port) {
        if (!open_socket()) return false;

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed\n";
            close_socket();
            return false;
        }

        running = true;
        recv_thread = std::thread(&UdpServer::receive_loop, this);

        std::cout << "Server started on port " << port << "\n";
        return true;
    }

    void stop() {
        if (running) {
            running = false;
            if (recv_thread.joinable())
                recv_thread.join();
            close_socket();
            std::cout << "Server stopped\n";
        }
    }

    void register_plugin(std::shared_ptr<IRudpPlugin> plugin) {
        plugins.push_back(plugin);
    }

    bool send_packet(const packet& pkt, const sockaddr_in& client_addr) {
        // 這裡簡單只發送 seqId，實際你會序列化 header + payload
        uint32_t seq = htonl(pkt.hdr.seqId);
        return send_to(client_addr, reinterpret_cast<uint8_t*>(&seq), sizeof(seq));
    }

private:
    void receive_loop() {

        while (running) {
            try {
                uint8_t buffer[1500];
                sockaddr_in client_addr{};
                socklen_t addr_len = sizeof(client_addr);

                int recv_len = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                        (sockaddr*)&client_addr, &addr_len);

                if (recv_len > 0) {
                    // 解析封包
                    packet pkt = PacketHelper::from_bytes(buffer, recv_len);

                    // 記錄 client 存活時間
                    clients[client_addr] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();

                    // 通知 plugins 處理
                    for (auto& plugin : plugins) {
                        plugin->on_receive(pkt, client_addr);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            } catch (const std::exception& ex) {
                std::cerr << "[Server] receive_loop exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[Server] receive_loop unknown exception\n";
            }
        }
    }

};

// ------ UDP Client --------

class UdpClient : public UdpSocketBase {
    sockaddr_in server_addr{};

public:
    UdpClient() = default;

    bool connect_to(const char* ip, uint16_t port) {
        if (!open_socket()) return false;

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
#ifdef _WIN32
        server_addr.sin_addr.S_un.S_addr = inet_addr(ip);
#else
        inet_pton(AF_INET, ip, &server_addr.sin_addr);
#endif
        server_addr.sin_port = htons(port);
        return true;
    }

    bool send_packet(const packet& pkt) {
        uint32_t seq = htonl(pkt.hdr.seqId);
        return send_to(server_addr, reinterpret_cast<uint8_t*>(&seq), sizeof(seq));
    }

    // 你可以自行加 receive 相關函式
};


#endif