# Interest management

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is T03 progress. It is pure per-client policy: no transport, and it is
not yet connected to `Replication`, `Wire` or the session service.

## Design

`src/net/interest.elisa` (`NetInterest`) decides, per client and entity, what
to send:

- **Relevance.** `relevant` is a squared-distance test against a radius, so
  it takes no square root. Out-of-range inputs are irrelevant, not trapped.
- **Plan.** `plan_for` picks `Skip` (out of range, unknown), `Enter` (in range,
  unknown: a full record), `Resend` (known but unacknowledged: the baseline is
  not trusted, so full again), `Delta` (acknowledged baseline) or `Leave` (out
  of range but known, so the client drops the reference).
- **Budget.** `charge` spends a plan's byte cost (full 33, delta 12, leave 9)
  only if it fits. Otherwise it defers the send as `Skip` and leaves the
  budget and the entity's state alone, so queues stay bounded.
- **State.** `after_send` records a send as known-but-unacknowledged, or
  forgets the entity on a leave. `acknowledge` trusts the baseline, and
  `lost` demotes it so the next send is full. A late joiner starts with
  nothing known, so every relevant entity arrives in full.
- `src/net/interest_math.elisa` holds `remaining_after` and `fits`, kept
  enum-free for the prover.

## Proof

`proof/net_interest.elisa` proves the budget after a charge stays within
`0..budget`. It proves 13/13 replayed. The prover cannot bound a product of
variables, so `distance_squared` is covered by tests, not proved.

## Checks

- `test/net_interest.elisa` exits 0 (codes 1–13): relevance and its bounds,
  a late joiner, unacknowledged resend, delta after an ack, loss recovery,
  leave then forget, deferral over budget, and spending a budget across
  entities.
- Negative control: trusting an unacknowledged baseline makes the test exit 6.
- The test is compiled and run by `scripts/check.elisascript`.

## Gaps

- No tie to world data: entity positions, subscriptions by cell and per-tick
  ordering are not modelled, and deferred entities are not prioritised.
- No real multi-client run and no measured bandwidth.
