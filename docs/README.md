# RUDP Docs

This folder explains the internal data structures and runtime state used by the
current prototype implementation.

Files:

- `Protocol.md`: current protocol draft and packet/layout notes
- `architecture.md`: high-level component diagram
- `project_overview.json`: machine-readable project summary for external review
- `session-types.md`: field-by-field explanation of the types declared in
  `include/Rudp/SessionTypes.hpp`
- `tx-rx-notes.md`: practical notes about how TX and RX state change during
  handshake, ACK tracking, retransmission, and delivery
