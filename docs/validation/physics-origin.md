# Floating-origin rebasing policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is P12 progress.

## Design

`src/physics/origin.elisa` (`PhysicsOrigin`) is the arithmetic behind origin
rebasing. Absolute positions are whole millimetres in i64; simulation and
rendering use offsets from an `Origin`.

- `axis_shift` returns zero while a coordinate is within the threshold, and
  otherwise a whole number of cells toward it. The shift is a multiple of the
  cell size so streamed-cell boundaries stay aligned. A zero threshold or
  cell, a cell wider than the threshold, or an absurd coordinate rebases
  nothing.
- `needs_rebase`, `plan` and `apply` work over three axes; only a real shift
  counts as a rebase.
- `to_local_x` and `to_world_x` convert with the origin, so a rebase leaves
  every world position unchanged and only the local numbers move.
- `spacing_um` estimates single-precision spacing at a distance (24-bit
  significand), showing how much precision a large local range costs.

## Checks

- `test/physics_origin.elisa` exits 0 (codes 1-14): threshold edges,
  positive and negative shifts, invalid parameters, world-position
  invariance across a rebase, no repeat rebase, no-op shifts and the
  spacing estimate.
- Negative control: returning the unrounded coordinate instead of whole
  cells fails the test (exit 3).
- Wired into `scripts/check.elisascript`.

## Gaps

- Only the x axis has local/world converters, and nothing yet shifts the
  physics bodies, render snapshots, audio emitters, nav data or saved
  coordinates when a rebase occurs. Whether Jolt is built with double
  precision has not been checked. No far-travel scene ran. P12 stays open.
- No proof written for this module.
