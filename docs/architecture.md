# Architecture

```mermaid
flowchart TD
    Udp[UDP Datagram] --> Codec[Codec::decode]
    Codec --> Manager[ServerSessionManager]

    Manager --> Pending[Pending sessions by endpoint]
    Manager --> Active[Active sessions by conn_id]

    Pending --> SessionP[Session]
    Active --> SessionA[Session]

    SessionP --> LifecycleP[Handshake / close / reset state]
    SessionA --> LifecycleA[Handshake / close / reset state]

    SessionA --> Tx[TxHandler]
    SessionA --> Rx[RxHandler]
    SessionP --> TxP[TxHandler]
    SessionP --> RxP[RxHandler]

    Tx --> AckTx[Apply remote Ack/AckBits]
    Tx --> Retx[Retransmit / backoff / retry limit]
    Tx --> ControlTx[Build SYN ACK FIN ACK-only]

    Rx --> AckRx[Track next_expected / AckBits]
    Rx --> Ordered[Reliable ordered reorder + contiguous drain]
    Rx --> Unordered[Reliable unordered immediate delivery]
    Rx --> Mono[Monotonic state filter]

    Manager --> Outbound[OutboundDatagram endpoint + bytes]
```

## Notes

- `ServerSessionManager` owns multi-session concerns:
  - new-peer session creation
  - global `conn_id` allocation
  - pending-to-active promotion
  - active dispatch
  - terminal cleanup
- `Session` owns per-connection meaning:
  - lifecycle state machine
  - transport ACK processing
  - channel delivery semantics
  - handshake linger and FIN acknowledgement behavior
- `poll_tx()` at the manager layer currently collects at most one datagram per
  pending session and one datagram per active session per poll cycle.
