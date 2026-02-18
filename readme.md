# RUDP
---

This project aims to implement a lightweight reliable protocol over UDP socket.

---

## Header format

The HeaderV1 structure is defined in `Rudp/Protocol.hpp`.  
Each field has a fixed size, and the network byte order is big-endian.  
The total header size is 28 bytes.

The following table describes the header fields.

| Field | Length (bytes) | Description |
|------|-----------------|------------|
| ConnId | 4 | Identifier of the connection (session). |
| Seq | 4 | Packet sequence number within the connection. |
| Ack | 4 | Highest contiguous sequence number received. |
| AckBits | 8 | Selective acknowledgment bitmap for packets before Ack. |
| TimestampMs | 4 | Sender timestamp in milliseconds (used to calculate RTT). |
| ChannelId | 1 | Channel identifier for this packet. |
| PayloadLen | 2 | Payload length in bytes (0 = ack-only or keepalive). |



