# ADR 0015: Submission storage ends at completion

**Status:** accepted (2026-09-19)

## Context

CPU simulation finishing does not prove that queued native or GPU work has
stopped reading its submission bytes. Resetting frame storage at that point
would permit use-after-reset behavior.

## Decision

`Lifetime::Queue` gives each submitted byte range a completion token. A queue
cannot reset while any submission is pending; completion compacts the bounded
queue and only then permits reset. The policy is independent of the native
backend and complements the FFI contract's `UntilCompletion` retention rule.

## Evidence

`src/runtime/lifetime.elisa` and the lifetime section of
`test/contracts.elisa` cover submission, pending-byte accounting, blocked
reset, completion, reset, invalid sizes, and queue invariants.
