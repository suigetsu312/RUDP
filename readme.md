# RUDP

> This project is an experimental implementation and is not complete yet.

RUDP (Reliable UDP) is a lightweight transport protocol built on top of UDP.

It provides configurable delivery semantics (reliable / ordered / unordered) while keeping the protocol small and implementation-oriented.

The full protocol specification is defined in `docs/Protocol.md`.

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

---

## Runtime Usage

The current prototype includes a BSD UDP runtime and YAML-based runtime
profiles.

Default profiles:

* `configs/server.yaml`
* `configs/client.yaml`

Transport timing defaults remain in:

* `.env`
* `.env.example`

Build:

```bash
cmake --preset default
cmake --build build
```

Run tests:

```bash
./build/unit_tests
```

Run directly:

```bash
./build/rudp_app configs/server.yaml
./build/rudp_app configs/client.yaml
```

Or use the helper script:

```bash
./scripts/run_rudp.zsh server
./scripts/run_rudp.zsh client
./scripts/run_rudp.zsh both
```

Weak-network experiment scaffold:

```bash
./scripts/run_netem_experiment.zsh baseline
./scripts/run_netem_experiment.zsh delay 100ms
./scripts/run_netem_experiment.zsh loss 5%
./scripts/run_netem_experiment.zsh reorder 25% 50%
./scripts/run_netem_experiment.zsh down
```

Recommended manual experiment matrix:

| Scenario | Netem | Channel | Suggested bootstrap |
| --- | --- | --- | --- |
| baseline-chat | none | `chat` / `Unreliable` | `/spawn chat 4 0 5 burst` |
| baseline-events | none | `events` / `ReliableUnordered` | `/spawn events 4 0 5 burst` |
| baseline-stream | none | `stream` / `ReliableOrdered` | `/spawn stream 4 0 5 burst` |
| delay-100ms-stream | `delay 100ms` | `stream` / `ReliableOrdered` | `/spawn stream 4 0 5 burst` |
| loss-5pct-events | `loss 5%` | `events` / `ReliableUnordered` | `/spawn events 4 0 5 burst` |
| reorder-25pct-stream | `reorder 25% 50%` | `stream` / `ReliableOrdered` | `/spawn stream 4 0 5 burst` |

If you want to run the whole matrix unattended, use:

```bash
# default: 600 seconds per case
./scripts/run_netem_matrix.zsh

# custom duration per case
./scripts/run_netem_matrix.zsh 300
```

Export final client/server summaries from all experiment logs into one CSV:

```bash
./scripts/export_experiment_csv.zsh
```

Default output:

```text
logs/experiment_summary.csv
```

Here `count=0` means "keep sending until the experiment is torn down".

The matrix runner will:

* start each experiment
* continuously send traffic for the configured duration
* tear it down so final per-session summaries are flushed to the log files
* move on to the next case automatically

This is useful when you want to leave the machine running for one or two hours
and inspect the final logs later.

The Docker experiment helper writes a bootstrap client command into
`tmp/client.commands`.

Experiment logs are grouped by experiment name and timestamp, for example:

```text
logs/baseline/20260407230512/server.log
logs/baseline/20260407230512/client.log
```

You can override the default load command, for example:

```bash
RUDP_CLIENT_COMMAND="/spawn events 4 0 2 burst" \
  ./scripts/run_netem_experiment.zsh delay 80ms
```

Docker-based experiment flow:

```bash
# baseline
./scripts/run_netem_experiment.zsh baseline

# inspect container status and generated logs
docker compose ps
ls logs/baseline

# delay / loss / reorder experiments
./scripts/run_netem_experiment.zsh delay 100ms
./scripts/run_netem_experiment.zsh loss 5%
./scripts/run_netem_experiment.zsh reorder 25% 50%

# tear everything down
./scripts/run_netem_experiment.zsh down
```

Docker image rebuild policy:

```bash
# default: only build when rudp-server / rudp-client images are missing
./scripts/run_netem_experiment.zsh baseline

# force rebuild after code changes
RUDP_DOCKER_BUILD=always ./scripts/run_netem_experiment.zsh baseline

# skip rebuild entirely and require existing images
RUDP_DOCKER_BUILD=never ./scripts/run_netem_experiment.zsh baseline
```

When you use the unattended matrix runner, `RUDP_DOCKER_BUILD=always` only
rebuilds the first case. The remaining cases reuse the same images so the
whole run does not spend time recompiling six times.

For `reorder`, the helper automatically applies a small baseline delay first
because `tc netem reorder ...` requires delay to be present. Override it with
`RUDP_NETEM_REORDER_DELAY`, for example:

```bash
RUDP_NETEM_REORDER_DELAY=40ms ./scripts/run_netem_experiment.zsh reorder 25% 50%
```

RTT fields are always printed in summaries. If you see `rtt_ms=n/a`, it means
that no RTT sample was collected during that run.

By default the client sends a low-frequency keepalive probe every `500ms`
while established. This keeps RTT samples flowing during sustained traffic
without turning `PING/PONG` into a high-rate control stream.

Useful runtime commands:

* client:
  * type plain text to send on the default channel
  * `send <channel> <message>`
  * `/spawn <channel> <threads> <count> <interval-ms> <payload>`
  * `/workers`
  * `/stop-load`
  * `/channels`
  * `/quit`
* server:
  * type plain text to send on the default channel to the most recent active session
  * `send <conn_id> <channel> <message>`
  * `/sessions`
  * `/channels`
  * `/quit`

Example session:

```bash
# terminal 1
./scripts/run_rudp.zsh server

# terminal 2
./scripts/run_rudp.zsh client
```

Then in the client terminal:

```text
/channels
send chat hello
send events event-1
send stream ordered-message
/spawn stream 4 0 5 burst

Use `count=0` if you want the workers to keep sending until `/stop-load`
or until the runtime is torn down.
```

Then in the server terminal:

```text
/sessions
send <conn_id> chat hello-client
```
