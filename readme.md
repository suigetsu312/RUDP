# RUDP

> This project is an experimental implementation and is not complete yet.

RUDP (Reliable UDP) is a lightweight transport protocol built on top of UDP.

It provides configurable delivery semantics (reliable / ordered / unordered / monotonic state) while keeping the protocol small and implementation-oriented.

The full protocol specification is defined in `protocol.md`.

---

## Motivation

UDP is connectionless and does not guarantee delivery or ordering.

TCP provides reliable and ordered delivery but introduces head-of-line blocking and strict retransmission behavior, which may increase latency under packet loss.

RUDP aims to provide:

* Reliable delivery without mandatory ordering
* Channel-level semantic separation
* Predictable retransmission behavior
* A minimal and implementation-focused design

It is primarily designed for:

* Real-time control systems
* Multiplayer game networking
* IoT control environments

---

## Design Overview

Key characteristics:

* Fixed 28-byte header (v1.1)
* Reliable-only sequence space
* 64-bit selective ACK bitmap (SACK)
* Sliding in-flight window (64 packets)
* Three-way handshake
* Strict reliable semantics (deliver-or-fail)
* Static channel configuration

The protocol intentionally excludes:

* Congestion control
* Encryption
* Packet fragmentation

These may be layered externally if required.

---

## Project Structure

```
Rudp/
 ├── Protocol.hpp     # Protocol constants and header definition
 ├── Codec.hpp        # Header encode/decode logic
 ├── Utils.hpp        # Endian utilities
 └── ...
tests/
 ├── test_codec_header_v1.cpp
 └── ...
protocol.md           # Formal protocol specification
```
