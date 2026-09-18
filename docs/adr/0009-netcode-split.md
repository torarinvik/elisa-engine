# ADR 0009: Transport moves bytes; Elisa owns netcode policy

**Status:** accepted (2026-09-18)

## Context

A socket library provides none of replication, authority,
interpolation, prediction, or recovery. Confusing transport health with
game-state agreement produces silent divergence.

## Decision

- Split: transport (bytes, e.g. GameNetworkingSockets later) versus
  replication records, per-client authority grants, interpolation data,
  and session lifecycle — all Elisa data with pure validators.
- Revision-ordered rollback per persistent identity; same-position
  newer revisions never trigger resimulation.
- Loss is modeled deterministically (`loss_every` drop pattern) and
  latency as explicit tick delays, so degraded-service behavior is
  demonstrated rather than asserted. Sessions degrade only within a
  declared profile and recover through `Recovering`, never by silently
  continuing diverged.
- Client writes require an explicit per-identity grant; server-owned
  identities reject client writes as data.

## Evidence

`src/net/replication.elisa`, `src/net/session.elisa`,
`test/replication.elisa`, `test/session.elisa`.

## Not covered

Transport integration, loss/latency chaos runs, demonstrated multiplayer
session, authority migration, snapshot compression.

## Client prediction (2026-09-18)

`src/net/prediction.elisa` adds the client half above the transport. Local
input applies to the predicted record immediately and is retained; each
authority snapshot becomes the confirmed record, and the predicted record is
rebuilt as the snapshot plus every unacknowledged input replayed in order. A
rebuilt position that differs counts as a correction, and acknowledging more
inputs than are pending is an explicit error. `test/replication.elisa` pins the
ahead-of-server error, the reconciliation, the correction count, the ack, and
the invalid-snapshot rejection.


## Wire decode hardening (2026-09-18)

`Wire::decode` now holds a decoded frame to the same validity contract as a
record the engine built: a crafted frame with a zero persistent identity or a
revision that wraps negative is refused with `InvalidRecord` instead of
entering the world. `test/session.elisa` adds an all-zero frame, an all-ones
revision, and an unrepresentably negative coordinate (refused at encode), so
the untrusted byte boundary is tested for malformed input, not only round
trips.

## Snapshot interpolation (2026-09-18)

`src/net/interpolation.elisa` keeps the two most recent authority snapshots and
renders between them at the client's own time. Sampling clamps to the held
range rather than extrapolating beyond what the server confirmed, the
revision and owner always come from the newer snapshot so an interpolated
position never masquerades as authority, and a reordered or invalid snapshot is
refused. `test/session.elisa` pins the single-snapshot case, the midpoint and
quarter interpolation, both clamps, and the out-of-order and invalid rejections.

## Wire decoder fuzz test (2026-09-18)

The plan asks for fuzzing at the unverified boundary. `test/session.elisa` now
mutates a valid frame with zero to three flipped bytes over 256 deterministic
rounds and requires the decoder to either accept a valid record or refuse with
one of its declared errors; any other outcome fails. It also requires both an
acceptance and a rejection to occur, so the test exercises both paths rather
than passing vacuously. Combined with the malformed-frame cases added earlier,
the untrusted byte boundary is covered by round trips, crafted bad frames, and
a fuzz sweep.
