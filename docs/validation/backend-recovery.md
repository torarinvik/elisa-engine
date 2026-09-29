# Rendering recovery policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is R14 progress. It is the failure-handling state machine only; no native
device, swapchain or upload failure is injected into Wicked or SDL3.

## Design

`src/backend/recovery.elisa` (`BackendRecovery`) reports four failure kinds.

- Upload and resize failures degrade but keep rendering on the previous
  resources; a good frame (`frame_ok`) returns to normal.
- Swapchain loss stops rendering and recovers without changing the epoch.
- Device loss bumps the `epoch` (so `handle_valid` rejects every older GPU
  handle) and sets `rebuild_pending`, meaning the scene is rebuilt from
  authoritative asset and world state rather than by re-running gameplay
  spawns. It escalates a swapchain recovery already under way.
- Attempts are bounded at 3 with waits of 2 then 4 frames; running out ends in
  `Failed`, which is final and ignores later reports and attempts.

## Checks

- `test/backend_recovery.elisa` exits 0 (codes 1–15).
- Negative control: not bumping the epoch on device loss makes it exit 7.
- Source-length check passes.

## Gaps

- Not connected to SDL3/Metal or Wicked, no injected native failures, and no
  proof that a rebuilt scene has no duplicated entities. R14's done condition is
  not met. Full gate still blocked at `world-test`.
