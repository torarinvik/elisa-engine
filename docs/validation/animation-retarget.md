# Retarget map and sockets

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is C06 progress. It is map validation and scaling policy only; no skeleton,
clip or pose is retargeted.

## Design

`src/animation/retarget.elisa` (`AnimationRetarget`):

- `Map` pairs up to 16 source bones with target bones (indices 0–63). `pair`
  refuses out-of-range bones, a source or target bone already used, and a full
  map, and a refused pair changes nothing, so two source bones can never drive
  one target bone.
- `scale_permille` is the ratio of two reference bone lengths; translation is
  scaled by it (`scale_translation`) while rotations pass through. Bad lengths
  give 0, an unusable rig.
- `socket_ok` accepts a bone within the skeleton's bone count and an offset
  within ±100 m.

## Checks

- `test/animation_retarget.elisa` exits 0 (codes 1–11).
- Negative control: allowing a repeated target bone makes it exit 3.
- Source-length check passes.

## Gaps

- No poses are retargeted, no evaluated-skeleton attachment follows blends,
  teleports or character destruction, and no two-rig clip is shown, so C06's
  done condition is not met. Full gate still blocked at `world-test`.
