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
