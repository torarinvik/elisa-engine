# Device and display fallback

Validated on 2026-09-29 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I03 progress.

## Design

- `src/runtime/device_choice.elisa` (`DeviceChoice`) resolves a saved device
  id against the devices present now: the saved device if present and usable,
  else the system default, else the first usable device, else none. The
  `Source` says which rule applied so the UI can explain it.
- The saved value is not rewritten by a fallback, so reconnecting a device
  restores the saved choice.
- `pick_mode` chooses the largest display mode not exceeding the saved size,
  or the smallest mode when none fits, or -1 with no modes.

## Checks

- `test/runtime_device_choice.elisa` exits 0 (codes 1–14): each fallback
  step, unusable saved device, capacity, invalid id, and display modes.
- Negative control: dropping the usable check on the saved device makes the
  test exit 5.

## Gaps

- No platform enumeration, settings persistence, clipboard, user-data paths
  or permission results; nothing calls this yet. I03 stays open. No proof
  harness.
