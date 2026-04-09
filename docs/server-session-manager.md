# Server Session Manager

`ServerSessionManager` is the first multi-session owner above a single
`Session`. It exists to solve problems that a lone `Session` cannot solve by
itself:

- identifying a peer before a connection is established
- allocating globally unique server-side `conn_id` values
- routing packets to the correct session
- promoting handshaking sessions into active sessions
- cleaning up terminal sessions

## Current Model

`Session` still represents one logical client/server interaction:

- handshake state
- `conn_id`
- TX/RX transport state
- events
- close/reset lifecycle

The manager owns *many* server-side sessions and decides which one should
receive each incoming datagram.

## Keys And Storage

The manager currently uses two primary indexes.

### Pending By Endpoint

```cpp
std::unordered_map<EndpointKey, Session> pending_by_endpoint_;
```

This stores server-side sessions that are known by remote UDP endpoint but are
not yet established.

The key is:

- source IP
- source UDP port

This is the only stable identity available before the handshake completes.

### Active By ConnId

```cpp
std::unordered_map<std::uint32_t, Session> active_by_conn_id_;
std::unordered_map<EndpointKey, std::uint32_t> active_conn_id_by_endpoint_;
```

Once a session becomes established, it is promoted into the active map and is
primarily routed by `conn_id`.

The endpoint side map is kept so the manager can:

- know where to send active-session outbound datagrams
- clean up both indexes together
- avoid creating a second pending session for an already-active endpoint

## Session Creation Rule

The current server rule is intentionally simple:

- if an incoming datagram comes from an unknown endpoint
- and that endpoint is neither pending nor already active
- create a new server-side `Session`
- allocate a fresh non-zero `conn_id`
- assign that `conn_id` into the new session

Handshake semantics remain inside `Session`.

This means the manager does **not** try to decide whether a packet is a valid
handshake before creating the session. It only decides whether the peer is new.

## Incoming Routing Order

`on_datagram_received(...)` currently routes in this order:

1. If `header.conn_id != 0`, try active-session dispatch first.
2. If no active session matched, try pending-session dispatch by endpoint.
3. If `conn_id == 0` and the endpoint is new, create a pending session and
   route the packet into it.
4. If `conn_id != 0` still did not match any active or pending route, drop it.

This keeps the meaning of non-zero `conn_id` strict:

- a non-zero `conn_id` claims to belong to an already-known connection
- unknown non-zero `conn_id` packets are treated as invalid and dropped

## Promotion

Pending sessions are promoted when their internal connection state becomes
`Established`.

Promotion does:

1. read the session's assigned `conn_id`
2. store `endpoint -> conn_id`
3. move the `Session` from `pending_by_endpoint_` to `active_by_conn_id_`

After promotion, future packets with that `conn_id` route directly to the
active session.

## Outbound Polling

The manager's `poll_tx(now_ms)` currently collects outbound datagrams from:

- every pending session, at most one packet each
- every active session, at most one packet each

It returns:

```cpp
struct OutboundDatagram {
  EndpointKey endpoint;
  std::vector<std::byte> bytes;
};
```

This is the first fairness policy:

- one manager poll
- one packet per session at most

It avoids letting a single session monopolize the server's send loop.

## Cleanup Policy

The manager currently treats these states as terminal:

- `Reset`
- `Closed`

If a pending or active session reaches either state, it is removed from the
manager tables immediately.

This matches the current project decision:

- `Reset` is terminal
- no quick reconnect / restore behavior is kept in the transport manager
- future reconnect behavior, if desired, should create a new session

## Responsibilities That Stay Inside `Session`

The manager does **not** own transport semantics. These still remain inside
`Session` and the lower handlers:

- handshake state transitions
- ACK / AckBits tracking
- retransmission and backoff policy
- ordered/unordered delivery policy
- handshake linger
- FIN / RST behavior
- event generation

The split is:

- manager decides *which session gets the datagram*
- session decides *what the datagram means*

## What Is Still Missing

The manager skeleton is now functional, but a few higher-level policies are
still open:

- idle-time cleanup / keepalive integration
- any future reconnect policy
