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
