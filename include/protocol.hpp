#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>
#include <vector>

enum class RUDP_STATUS  {
    CLOSED,
    SYN_SENT,      // 對方還沒回應
    ESTABLISHED,   // 可以正常傳送資料
    FIN_WAIT,      // 正在關閉中
    TIMEOUT        // 對方失聯
};

enum class RudpPacketType : uint8_t {
    DATA = 0,
    ACK,
    SYN,
    SYN_ACK,
    FIN,
    HEARTBEAT
};

#pragma pack(push, 1) // Ensure no padding is added to the structure
struct header {
    static constexpr uint32_t MAGIC = 0xABCD1234;
    static constexpr uint8_t VERSION = 1;
    RudpPacketType type;           // 是資料還是控制封包？
    uint16_t flags;                // 可用 bitmask 設定更多行為（壓縮、加密等）
    uint32_t seqId;                // 序列號
    uint32_t ackId;                // ACK 回報號
    uint32_t length;               // payload 長度
};
#pragma pack(pop) // Restore the previous packing alignment

struct packet {
    header hdr; // Header of the packet
    std::vector<uint8_t> payload; // Pointer to the payload data
};


#endif