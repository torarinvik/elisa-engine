# Headless server tick policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is T05 progress.

## Design

`src/net/server.elisa` (`NetServer`) turns wall-clock slices into a bounded
number of fixed simulation ticks. It uses no display, audio or platform
service, so it can back a headless server target.

- `advance` accumulates time, runs whole ticks up to `max_catch_up`, drops
  the backlog beyond that and counts it as an overrun with its dropped ticks,
  and carries the sub-tick remainder.
- Persistence is due on every `persist_every`-th tick.
- `request_shutdown` moves Running to Draining. Draining runs `drain_ticks`
  more ticks, then reaches Stopped with a final persist. Asking twice, or
  when stopped, changes nothing; a stopped loop never runs another tick.
- Metrics: ticks, overruns, dropped ticks, persists, worst backlog.
- Invalid configuration or negative time never advances the loop.

## Checks

- `test/net_server.elisa` exits 0 (codes 1-11): carry, exact persist
  cadence, a 200 ms stall (5 ticks run, 15 dropped, one overrun), drain to
  stop, idempotent shutdown, invalid inputs.
- Negative control: letting `ticks_due` exceed the cap fails a postcondition.
- Wired into `scripts/check.elisascript`.

## Gaps

- No proof: the prover cannot yet show a quotient of non-negative operands is
  non-negative, so `ticks_due` is checked by its runtime contracts only.
- This is policy only. There is no server executable, no real clock, no
  authored level, no remote clients and no storage flush yet, so T05 stays
  open.
