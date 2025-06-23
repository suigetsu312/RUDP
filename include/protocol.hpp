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

#pragma pack(push, 1) 
struct header {
    uint32_t MAGIC = 0xABCD1234;
    uint8_t VERSION = 1;
    RudpPacketType type;          
    uint16_t flags;               
    uint32_t seqId;               
    uint32_t ackId;                
    uint32_t length;            
};
#pragma pack(pop) 

struct packet {
    header hdr; 
    std::vector<uint8_t> payload; 
};


#endif