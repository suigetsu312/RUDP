# Protocol Comparison

This note compares the current RUDP prototype against several widely used
custom-UDP stacks and extracts a practical benchmark plan we can mirror.

## Scope

Reference stacks:

- [ENet](https://github.com/lsalzman/enet)
- [KCP](https://github.com/skywind3000/kcp)
- [RakNet reliability layer](https://www.jenkinssoftware.com/raknet/manual/Doxygen/classRakNet_1_1ReliabilityLayer.html)
- [SteamNetworkingSockets lanes](https://partner.steamgames.com/doc/api/ISteamnetworkingSockets)
- [RakNet programming tips](https://www.jenkinssoftware.com/raknet/manual/programmingtips.html)

## Summary

RUDP currently looks closest to:

- ENet in its session-oriented, game-style runtime and channel semantics
- KCP in its explicit ACK/retransmit tuning and latency-first experimentation

It is still behind RakNet and SteamNetworkingSockets in:

- mature prioritization / lane scheduling
- broader flow control / pacing policy
- reconnect and security story
- fragmentation / aggregation polish

## Feature Table

| Stack | Core positioning | Delivery types | Channel / lane model | ACK / retransmit style | RTT / keepalive | Flow control / pacing | Security / resume |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `RUDP` | session-based custom UDP transport with app runtime | `Unreliable`, `ReliableUnordered`, `ReliableOrdered` | app-visible channels + internal probe lane | cumulative ACK + AckBits, timeout retransmit, fast retransmit with evidence threshold | per-session probe RTT, idle timeout, probe lane `PING/PONG` | basic windowing and fairness; still tuning ACK cadence and ordered weak-network behavior | no cryptographic auth, no secure resume yet |
| `ENet` | reliable UDP networking library | reliable and unreliable packet delivery with per-channel sequencing | channels are first-class | reliable delivery with packet commands, acknowledgments, sequencing | peer RTT and throttle stats are part of host / peer model | bandwidth throttling and packet throttling are built in | not a secure transport |
| `KCP` | pure ARQ layer on top of UDP | reliable ARQ stream-like delivery | no app-facing channel taxonomy by default | selective retransmit, fast retransmit, configurable ACK delay, external update loop | RTT/RTO are central to the algorithm | strong emphasis on latency tuning; window and update interval are explicit knobs | no built-in secure session identity by default |
| `RakNet` | full game networking stack | reliable, ordered, unordered, sequenced variants | ordering channels plus message priority | reliability layer with ACKs, timeout handling, coalescence, splitting, simulator hooks | timeout and statistics support are built in | richer priority and reliability taxonomy than current RUDP | optional security features exist in the larger stack, but not comparable to modern secure transports |
| `SteamNetworkingSockets` | production-grade game transport | reliable and unreliable messages on lanes | lanes with priorities and weights | mature transport internals hidden behind connection API | connection quality and ping stats are exposed | explicit lane priority/weight scheduling | stronger production security story than this prototype |

## Notable Similarities

### RUDP vs ENet

- Both are connection/session oriented, not just bare packet codecs.
- Both expose channel-like delivery semantics suited to games.
- Both are practical to embed into a client/server runtime.

### RUDP vs KCP

- Both expose tuning knobs for retransmit behavior.
- Both rely on an outer loop / runtime to drive time-based progress.
- Both are easy to experiment with under synthetic weak-network conditions.

## Important Differences

### Where RUDP is simpler

- No fragmentation/reassembly layer yet.
- No mature priority system for multiple classes of traffic.
- No authenticated reconnect or secure resume.

### Where RUDP is already strong

- Clear split between `ReliableOrdered`, `ReliableUnordered`, and `Unreliable`.
- Session manager and `conn_id` ownership are explicit.
- Probe lane, activity ACK controls, Docker/netem scripts, and CSV export make it easy to study behavior.

## Public Performance Claims

There is no clean, apples-to-apples official benchmark shared by all of these
projects. The most concrete public latency claim among the reference stacks is
from KCP:

| Stack | Public claim / emphasis | Source |
| --- | --- | --- |
| `KCP` | trades roughly 10% to 20% extra bandwidth for lower latency, claiming around 30% to 40% lower average latency and roughly 3x lower worst-case latency versus TCP in its README | [KCP README](https://github.com/skywind3000/kcp) |
| `RakNet` | emphasizes choosing reliability types based on gameplay semantics and warns that reliable traffic costs bandwidth because every reliable packet must be acknowledged | [Programming Tips](https://www.jenkinssoftware.com/raknet/manual/programmingtips.html) |
| `SteamNetworkingSockets` | emphasizes lane weights/priorities and documents that only reliable messages on the same lane have a strong ordering guarantee | [ISteamNetworkingSockets](https://partner.steamgames.com/doc/api/ISteamnetworkingSockets) |
| `ENet` | emphasizes reliable UDP plus throttling/bandwidth management more than public benchmark marketing | [ENet repository](https://github.com/lsalzman/enet) |

## Practical Benchmark Plan

Instead of chasing vendor-specific benchmark numbers, we should mirror the
traffic categories these stacks are designed for.

### Traffic classes to mirror

| RUDP channel | Meaning | Comparable usage in other stacks |
| --- | --- | --- |
| `chat` | `Unreliable` | transient updates where the next sample supersedes the previous one |
| `events` | `ReliableUnordered` | reliable event/trigger delivery without HOL blocking |
| `stream` | `ReliableOrdered` | chat/logical command stream where order matters |

### Network conditions to mirror

| Scenario | Why it matters |
| --- | --- |
| `baseline` | confirms clean-path behavior and minimum retransmit overhead |
| `delay 100ms` | mirrors common WAN / game latency checks |
| `loss 5%` | checks whether reliability survives moderate packet loss |
| `reorder 5%` | realistic mild reordering |
| `reorder 25%` | stress case, not everyday Internet conditions |

### Metrics to compare

For each run, record:

- `sent`, `recv`
- `data_tx`, `data_rx`
- `ctrl_tx`, `ctrl_rx`
- `retx`
- `rtt_ms`, `rtt_avg_ms`, `rtt_min_ms`, `rtt_max_ms`
- whether the session resets or survives the full duration

### Suggested pass criteria

| Channel | Must hold |
| --- | --- |
| `chat` | low retransmit pressure and no unexpected reset under baseline/delay |
| `events` | all reliable events eventually delivered; ordering may vary |
| `stream` | delivered in-order to the application; no unexpected reset under baseline, delay, and moderate loss |

## What To Run Here

Current repository support already matches this plan well:

- single-run experiments:
  - `./scripts/run_netem_experiment.zsh baseline`
  - `./scripts/run_netem_experiment.zsh delay 100ms`
  - `./scripts/run_netem_experiment.zsh loss 5%`
  - `./scripts/run_netem_experiment.zsh reorder 25% 50%`
- matrix runs:
  - `./scripts/run_netem_matrix.zsh 10`
- CSV export:
  - `./scripts/export_experiment_csv.zsh`

## Current Takeaway

As of the latest experiments:

- `baseline` is healthy
- `delay 100ms` and moderate `loss` are usable test targets
- `ReliableOrdered` under `reorder 25%` is still a stress-case tuning area
- raising fast retransmit evidence threshold from `2` to `3` materially improved stability in that stress case
