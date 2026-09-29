# Versioned protocol admission

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is T02 progress. It is pure policy: no transport, and it is not yet
connected to `Wire` or the session service.

## Design

- `src/net/protocol.elisa` (`NetProtocol`):
  - `negotiate` picks the highest version both peers speak and the smaller
    message size and budget. A malformed hello (bad range, size over 1024 B,
    negative budget) or no overlap is incompatible.
  - `admit` is the gate every incoming message passes before it may touch
    the world. It returns a verdict and a peer state. Malformed (incompatible
    peer, sequence below 1, size below 1), Oversized, Duplicate (sequence at
    or below the highest accepted), Unauthorized (spawn or despawn of an
    entity the sender does not own) and OverBudget (RPC or event past the
    window budget) all return the peer state unchanged.
  - Accepted advances the sequence and spends budget on RPCs and events.
    `next_window` restores the budget.
- `src/net/protocol_math.elisa` holds the integer helpers (`smaller`,
  `larger`, `spend`), kept enum-free so they can be proved.

## Proof

`proof/net_protocol.elisa` proves the negotiated limit is at most either
peer's, and that spending never takes the budget below zero. It proves 21/21
replayed. The prover does not model enum equality, so the `admit` decision
itself is covered by the test, not proved.

## Checks

- `test/net_protocol.elisa` exits 0 (codes 1–14): negotiation, incompatible,
  malformed and oversized hellos, an incompatible peer, accepted spawns,
  replays, stale sequences, oversized and zero-size messages, unowned
  despawns, budget exhaustion and a fresh window.
- Negative control: making a repeated sequence acceptable (`<` in place of
  `<=`) makes the test exit 7.
- The test is compiled and run by `scripts/check.elisascript`.

## Gaps

- Not connected to the wire format or session service: no negotiated
  schema on the wire, no entity spawn/despawn messages, no real
  authorization source.
- No per-message-type limits beyond size and window budget, and no
  reconnect handling of the sequence.
