# Multiplayer test laboratory

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is T06 progress. It builds on [`net-lag.md`](net-lag.md).

## Design

`src/net/lab.elisa` (`NetLab`) derives link faults from a seed with a
Park-Miller step: delay 0-8 ticks, loss every 0 or 2-5 snapshots, reordering,
and a bump size. It runs the lag model and checks the report against explicit
`Bounds` (peak error, pending buffer, final error).

A `Verdict` carries the seed, the lab build (`BUILD`), a checksum of the whole
report (`trace`), the pass flag and the report. Re-running a seed gives the
same trace, so a failure can be replayed. `first_failure` sweeps a range of
seeds and returns the first failing one.

Finding: a link that loses every snapshot (`loss_every` 1) never converges, so
it is outside the convergence contract. The generator never picks it, and the
lab reports it as a failure when run explicitly.

## Checks

- `test/net_lab.elisa` exits 0 (codes 1-9): replay identity, distinct faults
  per seed, valid derived conditions, a 300-seed sweep within bounds
  (peak error 40, buffer 16, final error 0), an impossible bound reported with
  its seed, and the dead-link case.
- Negative control: making `first_failure` report passes instead of failures fails code 5.
- Wired into `scripts/check.elisascript`.

## Gaps

- Faults are injected into the in-process lag model only. There is no
  recording of real traffic, no protocol fuzzing, no reconnect or late-join
  scenarios and no multi-client sessions, so T06 stays open.
- No proof; this is a test harness.
