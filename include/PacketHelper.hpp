#ifndef PACKET_HELPER_CPP
#define PACKET_HELPER_CPP

#include "protocol.hpp"
#include <vector>
#include <cstring>
#include <stdexcept>

class PacketHelper {
public:
    static std::vector<uint8_t> to_bytes(const packet& pkt) {
        std::vector<uint8_t> result(sizeof(header) + pkt.payload.size());
        std::memcpy(result.data(), &pkt.hdr, sizeof(header));
        if (!pkt.payload.empty()) {
            std::memcpy(result.data() + sizeof(header), pkt.payload.data(), pkt.payload.size());
        }
        return result;
    }

    static packet from_bytes(const uint8_t* data, size_t len) {
        if (len < sizeof(header)) {
            throw std::runtime_error("Packet too short");
        }

        packet pkt;
        std::memcpy(&pkt.hdr, data, sizeof(header));

        if (pkt.hdr.length > 0) {
            if (len < sizeof(header) + pkt.hdr.length) {
                throw std::runtime_error("Incomplete payload");
            }
            pkt.payload.resize(pkt.hdr.length);
            std::memcpy(pkt.payload.data(), data + sizeof(header), pkt.hdr.length);
        }

        return pkt;
    }
    /// @brief Create a mock data packet for testing or simulation purposes.
    /// @param seqId 
    /// @param data 
    /// @return 
    static packet create_data_packet(uint32_t seqId, const std::vector<uint8_t>& data) {
        packet pkt;
        pkt.hdr.MAGIC = 0xABCD1234;
        pkt.hdr.VERSION = 1;

        pkt.hdr.flags = 0x01; 
        pkt.hdr.type = RudpPacketType::DATA; 
        pkt.hdr.seqId = seqId;
        pkt.hdr.ackId = 0; 
        pkt.hdr.length = static_cast<uint32_t>(data.size());
        pkt.payload = data;

        return pkt;
    }

};

#endif
