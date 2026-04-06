# TX / RX Notes

This file is a practical companion to `session-types.md`. It focuses on how the
state fields are used at runtime.

## Outbound Flow

The sender path roughly looks like this:

1. app calls `Session::queue_send(...)`
2. `TxHandler::queue_app_data(...)` pushes a `SendRequest` into
   `tx.pending_send`
3. app later calls `Session::poll_tx(now_ms)`
4. `TxHandler::poll(...)` chooses one of these priorities:
   - handshake/control
   - retransmit
   - ACK-only
   - fresh app data

This priority matters a lot:

- connection setup/teardown can preempt app traffic
- retransmission can preempt fresh sends
- pure ACK packets are only sent when nothing better can carry the ACK

## Why `queue_send(...)` Does Not Immediately Create a Packet

Sequence numbers are assigned late on purpose.

Reasons:

- reliable window may currently be full
- handshake/control packet may need to go first
- retransmit may need to go first
- the queued data may be unreliable and not need a reliable sequence number at
  all

This is why `SendRequest` exists separately from `OwnedPacket`.

## ACK Model

Receiver state:

- `rx.next_expected` = cumulative ACK front
- `rx.received_bits` = selective ACK bits for future packets

Sender state:

- `tx.remote_ack` = peer's cumulative ACK
- `tx.remote_ack_bits` = peer's selective ACK bits

That means:

- RX describes what this endpoint has received
- TX describes what the peer says it has received

## `fast_retx_pending`

This flag belongs to `TxEntry`, not to the whole TX state.

That is important: fast retransmit is not a session-wide mode, it is a property
of one specific inflight packet.

Typical intended lifecycle:

1. packet is sent and stored in `tx.inflight`
2. later ACK information suggests a hole before later packets
3. the missing packet's `fast_retx_pending` becomes `true`
4. next `TxHandler::try_build_retransmit(...)` picks it immediately
5. after retransmission, the flag is cleared

This is different from timeout-based retransmission:

- timeout retransmit depends on `last_send_ms`
- fast retransmit depends on ACK pattern analysis

## Why `RxHandler` Does Not Send ACKs Directly

`RxHandler` only marks intent:

- `rx.should_ack = true`

Then `Session` converts that into:

- `tx.ack_only_pending = true`

This design keeps the send path centralized in TX code, so outbound packet
selection still follows one priority chain.

## Reliable Ordered vs Reliable Unordered

Both use reliable sequence tracking, but their delivery policy differs.

### Reliable Ordered

- packet contributes to ACK bookkeeping
- packet may be stored for later in-order delivery
- current contiguous drain behavior is still TODO

### Reliable Unordered

- packet contributes to ACK bookkeeping
- packet can be delivered immediately if it is not stale, duplicate, or outside
  the tracked ACK bitmap window

This is why ACK bookkeeping and application delivery policy are intentionally
separate concerns.

## Control Packets vs Data Packets

Control packets affect connection lifecycle and ACK state, but they are not
application payload events.

Examples:

- `SYN`
- `SYN-ACK`
- final handshake `ACK`
- `FIN`
- `RST`

These are classified first, then the connection state machine decides how they
change lifecycle state and what follow-up TX work should be scheduled.

## `drain_events()`

`drain_events()` means:

- return all currently queued `SessionEvent`s
- empty the internal event queue

So the function is both:

- a read
- a clear

That is why tests often call it after handshake steps just to discard old
events before checking the next stage.
