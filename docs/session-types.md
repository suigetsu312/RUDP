# Session Types

This document explains the purpose of every type declared in
`include/Rudp/SessionTypes.hpp`.

## Big Picture

`SessionState` is the in-memory state of one RUDP connection.

It is split into:

- `role`: whether this endpoint is the client or the server
- `connection_state`: where the connection lifecycle currently is
- `tx`: sender-side transport state
- `rx`: receiver-side transport state

The protocol code uses this separation on purpose:

- `TxHandler` focuses on what to send next
- `RxHandler` focuses on what has been received and whether it should be
  delivered
- `Session` orchestrates the two and applies connection lifecycle decisions

## `SessionRole`

```cpp
enum class SessionRole : std::uint8_t {
  Client = 0,
  Server = 1,
};
```

This tells the connection which side it is playing during handshake.

- `Client` actively starts connection establishment by sending `SYN`
- `Server` waits for `SYN` and responds with `SYN-ACK`

This value does not change after `Session` construction.

## `ConnectionState`

```cpp
enum class ConnectionState : std::uint8_t {
  Closed = 0,
  HandshakeSent = 1,
  HandshakeReceived = 2,
  Established = 3,
  Closing = 4,
  Reset = 5,
};
```

This is the connection lifecycle state machine.

- `Closed`: no connection has been established yet
- `HandshakeSent`: the client already sent `SYN`
- `HandshakeReceived`: the server already received `SYN` and is waiting for the
  final ACK
- `Established`: handshake is complete and normal data transfer is allowed
- `Closing`: a `FIN` was sent or received and the connection is closing
- `Reset`: the connection was aborted by `RST`

This is connection-level state, not per-channel state.

## `OwnedPacket`

```cpp
struct OwnedPacket final {
  Rudp::Header header;
  std::vector<std::byte> payload;
};
```

This is an owning packet container.

It exists because `Codec::decode(...)` produces `PacketView`, whose payload is a
borrowed span into the incoming datagram buffer. Borrowed payload is fine for
temporary parsing, but not safe to store long-term.

Use cases:

- storing an inflight retransmittable packet in `tx.inflight`
- storing an out-of-order ordered-reliable packet in
  `rx.ordered_reorder_buffer`

## `SendRequest`

```cpp
struct SendRequest final {
  std::uint32_t channel_id = 0;
  Rudp::ChannelType channel_type = Rudp::ChannelType::Unreliable;
  std::vector<std::byte> payload;
};
```

This is an application-level send request before it becomes a real packet.

The flow is:

1. the caller invokes `Session::queue_send(...)`
2. the request is pushed into `tx.pending_send`
3. a later `poll_tx(now_ms)` call turns it into a real outbound packet

Why not packetize immediately?

- sequence numbers are assigned only when a packet is actually chosen for
  transmission
- the send queue can be blocked by the reliable window limit
- handshake/control packets may be prioritized ahead of app data

## `TxEntry`

```cpp
struct TxEntry final {
  OwnedPacket packet;
  std::uint64_t first_send_ms = 0;
  std::uint64_t last_send_ms = 0;
  std::uint32_t retry_count = 0;
  bool fast_retx_pending = false;
};
```

This represents one reliable or control packet that has already been sent and is
waiting to be acknowledged by the peer.

### `packet`

The exact packet that was sent. It is stored so the sender can retransmit it
later without rebuilding it from scratch.

### `first_send_ms`

The first transmission timestamp.

Typical future uses:

- RTT measurement
- delivery latency statistics
- total age / give-up policies

### `last_send_ms`

The most recent transmission timestamp.

Current use:

- timeout-based retransmission in `TxHandler::try_build_retransmit(...)`

### `retry_count`

How many retransmissions were attempted so far.

Current use is small, but this field is the natural place to support:

- exponential backoff
- max retry limits
- debugging retransmission behavior

### `fast_retx_pending`

This marks an inflight packet for immediate retransmission on the next TX poll,
without waiting for the normal retransmission timeout.

Intended meaning:

- `false`: resend only if timeout expires
- `true`: resend as soon as TX scheduling reaches retransmit selection

Current code already consumes this flag in
`TxHandler::try_build_retransmit(...)`, but the logic that sets the flag from
incoming selective ACK gap information is still TODO in
`TxHandler::on_remote_ack(...)`.

In short:

- timeout retransmit = "we waited too long"
- fast retransmit = "the peer seems to have received later packets, so this one
  is probably missing"

## `SessionEvent`

```cpp
struct SessionEvent final {
  enum class Type {
    DataReceived,
    Connected,
    ConnectionClosed,
    ConnectionReset,
    Error,
  };

  Type type = Type::DataReceived;
  std::uint32_t channel_id = 0;
  Rudp::ChannelType channel_type = Rudp::ChannelType::Unreliable;
  std::vector<std::byte> payload;
  std::string error_message;
};
```

This is the public event type returned to the application through
`Session::drain_events()`.

It is intentionally simple: receive-side code stores events into
`rx.pending_events`, and the application pulls them later.

### `Type`

- `DataReceived`: application payload arrived
- `Connected`: handshake completed
- `ConnectionClosed`: graceful close was observed
- `ConnectionReset`: abort/reset was observed
- `Error`: protocol anomaly or invalid combination

### `channel_id`

The logical channel for `DataReceived`.

For connection-level events, this is usually left at `0`.

### `channel_type`

The delivery semantic of the received packet:

- `ReliableOrdered`
- `ReliableUnordered`
- `Unreliable`
- `MonotonicState`

### `payload`

The actual application bytes for `DataReceived`.

For non-data events this is normally empty.

### `error_message`

Extra context for `Error` events.

## `TxSessionState`

```cpp
struct TxSessionState final {
  std::uint32_t next_seq = 0;
  std::uint32_t remote_ack = 0;
  std::uint64_t remote_ack_bits = 0;
  std::deque<SendRequest> pending_send;
  std::map<std::uint32_t, TxEntry> inflight;
  bool syn_ack_pending = false;
  bool final_ack_pending = false;
  bool fin_pending = false;
  bool ack_only_pending = false;
};
```

This is the sender-side transport state.

## `TxSessionState::next_seq`

The next reliable sequence number to assign.

Important detail:

- sequence numbers are assigned when a packet is actually selected for
  transmission
- they are not assigned when the app only queues a send request

This means `queue_send(...)` is not "send now", it is "send later when TX
chooses it".

## `TxSessionState::remote_ack`

The latest cumulative ACK value reported by the peer.

Interpretation:

- packets older than this ACK are considered delivered to the remote side

It is updated in `TxHandler::on_remote_ack(...)`.

## `TxSessionState::remote_ack_bits`

The latest selective ACK bitmap reported by the peer.

Interpretation relative to `remote_ack`:

- bit 0 acknowledges `remote_ack + 1`
- bit 1 acknowledges `remote_ack + 2`
- and so on

This is used to detect out-of-order reception on the peer side and is the
natural input for future fast retransmit logic.

## `TxSessionState::pending_send`

Application payloads waiting to become packets.

This queue stores requests that were accepted by `queue_send(...)` but have not
been serialized and transmitted yet.

## `TxSessionState::inflight`

Reliable/control packets that were transmitted and are waiting for remote ACK.

The key is the reliable sequence number.

This is the sender's "unconfirmed delivery" table.

## `TxSessionState::syn_ack_pending`

Schedule a `SYN-ACK` packet on the next transmit opportunity.

Used by the server after receiving the client's initial `SYN`.

## `TxSessionState::final_ack_pending`

Schedule the final ACK of the three-way handshake on the next transmit
opportunity.

Used by the client after receiving `SYN-ACK`.

## `TxSessionState::fin_pending`

Schedule a `FIN` packet on the next transmit opportunity.

Used when the connection enters closing behavior.

## `TxSessionState::ack_only_pending`

Schedule a pure ACK packet with no payload.

This is usually set when RX transport bookkeeping decides an ACK response should
be sent, but there is no payload packet ready to piggyback that ACK.

## `RxSessionState`

```cpp
struct RxSessionState final {
  std::uint32_t next_expected = 0;
  std::uint64_t received_bits = 0;
  std::map<std::uint32_t, OwnedPacket> ordered_reorder_buffer;
  std::uint32_t next_ordered_delivery = 0;
  bool ordered_delivery_started = false;
  std::unordered_map<std::uint32_t, std::uint32_t> monotonic_versions;
  std::vector<SessionEvent> pending_events;
  bool should_ack = false;
};
```

This is the receiver-side transport and delivery state.

## `RxSessionState::next_expected`

The receiver's cumulative ACK front.

Interpretation:

- this is the next reliable sequence number still missing
- packets older than this are already behind the cumulative ACK front

In outbound headers, this becomes `ack`.

## `RxSessionState::received_bits`

Selective ACK bitmap for packets after `next_expected`.

Interpretation relative to `next_expected`:

- bit 0 means `next_expected + 1` has already been received
- bit 1 means `next_expected + 2` has already been received
- and so on up to the bitmap window size

This is a sliding window, not a full history log.

Example:

- `next_expected = 100`
- bit 0 = 0 means packet 101 has not been seen yet
- bit 1 = 1 means packet 102 already arrived early

If packet 100 later arrives, `next_expected` can advance and already-set bits
let the ACK front move across contiguous previously received packets.

## `RxSessionState::ordered_reorder_buffer`

Temporary storage for `ReliableOrdered` packets that arrived but cannot be
delivered in order yet.

Current status:

- packets are stored here
- contiguous in-order drain logic is still TODO

## `RxSessionState::next_ordered_delivery`

The next ordered reliable sequence number that is eligible for application
delivery.

This is intentionally separate from `next_expected`:

- `next_expected` is the transport ACK front
- `next_ordered_delivery` is the app-facing ordered delivery front

Those two values are related, but they are not the same responsibility.

## `RxSessionState::ordered_delivery_started`

Whether ordered delivery tracking has been initialized yet.

This avoids overloading `0` as a magic sentinel, which is especially important
because reliable sequence `0` can be valid after wrap-around.

## `RxSessionState::monotonic_versions`

Per-channel latest version for `MonotonicState`.

This prevents old state snapshots from overwriting newer ones.

## `RxSessionState::pending_events`

Accumulated outward-facing events waiting for the application to consume them.

`Session::drain_events()` empties this vector and returns its content.

## `RxSessionState::should_ack`

A bridge flag from RX bookkeeping to TX scheduling.

Meaning:

- RX observed something that should trigger an ACK response
- `Session` later turns that into `tx.ack_only_pending = true` if no other
  outbound packet already carries the ACK

This keeps RX from sending directly and preserves `Session` as the orchestrator.

## `SessionState`

```cpp
struct SessionState final {
  SessionRole role = SessionRole::Client;
  ConnectionState connection_state = ConnectionState::Closed;
  TxSessionState tx;
  RxSessionState rx;
};
```

This is the root state object for one connection.

Most of the implementation passes references to parts of this state instead of
copying data around.

Summary:

- `role`: static identity of the endpoint
- `connection_state`: lifecycle state machine
- `tx`: sender-side transport state
- `rx`: receiver-side transport and delivery state

If you want a single mental model for the file:

- `SessionState` is the whole protocol brain
- `TxSessionState` decides what leaves the socket
- `RxSessionState` remembers what has arrived and what the app should be told
