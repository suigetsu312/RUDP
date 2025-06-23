#ifndef RUDP_SOCKET_HPP
#define RUDP_SOCKET_HPP

#include "protocol.hpp"
#include "PacketHelper.hpp"
#include <iostream>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cstring> 
#include <thread>
#include <atomic>
#include <chrono>
#include "Plugin.hpp"
#include "SocketTools.hpp"

class UdpSocketBase {
protected:
    SOCKET sock = INVALID_SOCKET;
    std::vector<std::shared_ptr<IRudpPlugin>> plugins;

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

    void register_plugin(std::shared_ptr<IRudpPlugin> plugin) {
        plugins.push_back(plugin);
    }

    void unregister_plugin(std::shared_ptr<IRudpPlugin> plugin) {
        plugins.erase(std::remove(plugins.begin(), plugins.end(), plugin), plugins.end());
    }

protected:

    void notify_send(const packet& pkt, const sockaddr_in& addr) {
        for (auto& p : plugins)
            p->on_send(pkt, addr);
    }

    void notify_receive(const packet& pkt, const sockaddr_in& addr) {
        for (auto& p : plugins)
            p->on_receive(pkt, addr);
    }

};

// ------ UDP Server --------

class UdpServer : public UdpSocketBase {

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

    bool send_packet(const packet& pkt, const sockaddr_in& client_addr) {
        bool sent = send_to(client_addr, PacketHelper::to_bytes(pkt).data(), PacketHelper::to_bytes(pkt).size());
        if (sent) {
            notify_send(pkt, client_addr);
        } else {
            std::cerr << "Failed to send packet to client\n";
        }
        return sent;
    }

private:
void receive_loop() {
    while (running) {
        try {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);

            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 10000; // 10ms

            int activity = select(sock + 1, &readfds, nullptr, nullptr, &timeout);

            if (activity > 0 && FD_ISSET(sock, &readfds)) {
                uint8_t buffer[1500];
                sockaddr_in client_addr{};
                socklen_t addr_len = sizeof(client_addr);

                int recv_len = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                        (sockaddr*)&client_addr, &addr_len);

                if (recv_len > 0) {
                    packet pkt = PacketHelper::from_bytes(buffer, recv_len);

                    clients[client_addr] = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    notify_receive(pkt, client_addr);
                }
            }
            // else: timeout, loop again and check `running`
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

public:
    UdpClient() = default;
    ~UdpClient() {
        stop();
    }
    bool start(const char* ip, uint16_t port) {
        if (!open_socket()) return false;

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
#ifdef _WIN32
        server_addr.sin_addr.S_un.S_addr = inet_addr(ip);
#else
        inet_pton(AF_INET, ip, &server_addr.sin_addr);
#endif
        server_addr.sin_port = htons(port);

        running = true;
        recv_thread = std::thread(&UdpClient::receive_loop, this);
        std::cout << "Client starting to listen msg from server\n";

        return true;
    }
    void stop() {

        if (running) {
            running = false;
            if (recv_thread.joinable())
                recv_thread.join();
            close_socket();
            std::cout << "Client stopped\n";
        }
    }
    
    bool send_packet(const packet& pkt) {
        bool sent = send_to(server_addr, PacketHelper::to_bytes(pkt).data(), PacketHelper::to_bytes(pkt).size());
        if (sent) {
            notify_send(pkt, server_addr);
        } else {
            std::cerr << "Failed to send packet to server\n";   
        }
        return sent;
    }

private:
    sockaddr_in server_addr{};
    std::atomic<bool> running{false};
    std::thread recv_thread;

    void receive_loop() {
        fd_set readfds;
        struct timeval timeout;

        while (running) {

            try
            {
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                timeout.tv_sec = 0;
                timeout.tv_usec = 10000; // 10ms

                int activity = select(sock + 1, &readfds, nullptr, nullptr, &timeout);

                if (activity > 0 && FD_ISSET(sock, &readfds)) {
                    uint8_t buffer[1500];
                    socklen_t addr_len = sizeof(server_addr);

                    int recv_len = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                            (sockaddr*)&server_addr, &addr_len);

                    if (recv_len > 0) {
                        packet pkt = PacketHelper::from_bytes(buffer, recv_len);
                        notify_receive(pkt, server_addr);
                    }
                }

            }
            catch (const std::exception& ex) 
            {
                std::cerr << "[Client] receive_loop exception: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[Client] receive_loop unknown exception\n";
            }
        }
   
    }


};


#endif