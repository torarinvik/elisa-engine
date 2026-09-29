# Touch and haptics policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is I09 progress.

## Design

- `src/ui/touch.elisa` (`UiTouch`) tracks up to four fingers in bounded slots
  keyed by the device finger id.
  - `down` rejects an invalid id, a repeated id, or a fifth finger, which is
    ignored rather than queued.
  - `up` classifies the press: a Tap when it moved within the slop (24) and
    was released within 300 ms, otherwise a Drag; an unknown id gives None.
  - `cancel` frees one slot and reports Cancelled, never a tap or drag.
    `disconnect` drops every contact at once and returns how many, so a game
    never keeps a finger that is gone.
  - `spread` and `pinch_permille` give a two-finger pinch as a scale in
    permille of the starting spread, kept within 250..4000.
  - `haptic` clamps strength to 0..1000 and duration to 2 s, and answers
    `unavailable` (not played) on a device without rumble or touch feedback,
    so gameplay continues and the loss is disclosed.
- `src/ui/touch_math.elisa` holds the integer helpers.

## Proof

`proof/ui_touch.elisa` proves haptic strength and duration are always within
the safe range and that the Manhattan distance is never negative for any
finger travel within +-1e9 (39/39 replayed).

Findings for the prover, noted rather than fixed here:

- Negating a possibly `i64::MIN` value is unproven; a `requires value > -1e12`
  bound makes it provable.
- A call's `ensure` is not carried through a local for `magnitude`, so the
  absolute values are inlined in `manhattan`.
- The proof run prints two "semantic" static-precondition notes for the
  `manhattan` call in the harness even though the state is proved and the
  exit code is 0.

## Checks

- `test/ui_touch.elisa` exits 0 (codes 1-19): tap, drag by distance and by
  hold, invalid/duplicate/excess fingers, cancel, disconnect, pinch, haptic
  clamping and unavailable devices.
- Negative control: a disconnect that fails to release contacts fails the
  test.
- Wired into `scripts/check.elisascript`.

## Gaps

- No SDL3 event source or controller vibration binding yet; the policy takes
  already-decoded pointer events.
- Only tap, drag and pinch. No long-press, swipe or rotate recognisers.
- `pinch_permille` is nonlinear and unproven.
- No touch/controller example scene runs on a device, so I09 stays open.
