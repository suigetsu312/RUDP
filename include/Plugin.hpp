#ifndef PLUGIN_HPP
#define PLUGIN_HPP
#include "SocketTools.hpp"
#include "PacketHelper.hpp"
#include <iostream>
struct IRudpPlugin {
    virtual void on_receive(const packet& pkt, const sockaddr_in& client_addr) = 0;
    virtual void on_send(const packet& pkt, const sockaddr_in& client_addr) = 0;
    virtual ~IRudpPlugin() = default;
};

struct ConsoleLogPlugin : public IRudpPlugin {
    std::string name;

    ConsoleLogPlugin(const std::string& n) : name(n) {}

    void on_receive(const packet& pkt, const sockaddr_in& addr) override {
        std::cout << "[" << name << "] Received from "
                  << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port)
                  << " | Seq: " << pkt.hdr.seqId
                  << " | Payload: " << std::string(pkt.payload.begin(), pkt.payload.end())
                  << " | len =" << sizeof(pkt)
                  << "\n";
    }

    void on_send(const packet& pkt, const sockaddr_in& addr) override {
        std::cout << "[" << name << "] Sent to "
                  << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port)
                  << " | Seq: " << pkt.hdr.seqId
                  << " | Payload: " << std::string(pkt.payload.begin(), pkt.payload.end())
                  << " | len =" << sizeof(pkt)
                  << "\n";
    }
};


#endif