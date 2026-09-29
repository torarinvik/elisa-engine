# Lag scenarios for prediction

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is T04 progress. It follows the existing prediction model
(`src/net/prediction.elisa`).

## Design

`src/net/lag.elisa` (`NetLag`) runs a deterministic scenario: a client moves
one step per tick and sends each input, the server applies inputs `delay`
ticks later, bumps the body every `bump_every` ticks until `bump_until`, and
sends a snapshot every tick. Snapshots arrive `delay` ticks later; every
`loss_every`-th is lost, and with `reorder` odd ones arrive a tick late. The
client rejects stale snapshots, acknowledges applied inputs, and reconciles
through `Predict`. Delay is limited to 20 ticks. A full input buffer (16)
stalls the client rather than growing.

The `Report` gives the largest gap between the guess and the truth
(`max_error`), the largest pending buffer, corrections, blocked inputs,
stale and lost snapshots, and the final error.

## Checks

- `test/net_lag.elisa` exits 0 (codes 1-9): a calm link has no error; a
  3-tick delay with 5-unit bumps peaks at exactly 5; loss and reordering
  still converge to zero final error; reordering rejects stale snapshots; a
  20-tick delay saturates the 16-slot buffer and stalls; delay and loss and
  reordering together converge; invalid conditions are rejected.
- Negative control: never rejecting stale snapshots makes the test fail.
- Wired into `scripts/check.elisascript`.

## Gaps

- This is a one-axis model in-process. It does not yet drive the real
  transport, the character loop, or two processes, so T04 stays open.
- No proof; the scenario runner is a loop over mutable world state.
- Solver determinism limits are not yet stated beyond the existing note in
  `replication.elisa`.
