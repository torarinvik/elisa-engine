# KTX2 GPU texture memory budget (A06)

Recorded 2026-10-01.

## What it does

RenderScene counts the GPU bytes of every KTX2 snapshot texture it uploads,
in the format the device query selected (BC1/BC3/BC5/BC6H/BC7, ASTC 4x4, or
the RGBA8/RGBA16F fallback), summed over all mips, cube faces and material
roles. The scene-wide budget defaults to 512 MiB
(`MAX_SNAPSHOT_KTX2_GPU_BYTES`).

- `native/render_scene_snapshot_internal.inc` `assign_budgeted_snapshot_texture`
  passes the remaining budget to the uploader for both loose `.ktx2` files and
  ELPK KTX2 sections, and records the uploaded bytes per texture slot and role.
  The total is summed over live slots, so unregistering a texture returns its
  bytes without separate release bookkeeping.
- `native/ktx2_upload.h` checks each transcoded mip against
  `min(byte_limit, 64 MiB)` before `CreateTexture`, so an upload over budget
  fails before any GPU allocation and leaves the cached resource empty.
- A failed upload fails the material registration; the resident total is
  unchanged.
- `src/backend/texture_gpu_budget.elisa` is the admission rule (`remaining`,
  `fits`). `proof/texture_gpu_budget.elisa` proves that the remainder never
  exceeds the budget, that an upload larger than the budget is refused, and
  that a full (or over-full, after a lowered budget) scene admits nothing.
- The prover could not show completeness (every request that fits the
  remainder is admitted): a disjunction or implication over `budget - used`
  hits its wrap-guard and connective gates. The unit test covers that case
  with concrete exact-fit values.

Not budgeted: PNG/JPEG snapshot textures loaded through Wicked's resource
manager. Bundle PNG/JPEG textures keep their existing source and decoded
budgets.

## Evidence

- `test/backend_texture_gpu_budget.elisa` (in the check list): exact fit,
  one-byte-over refusal, full and over-full scenes, and values near u64 max.
- Render smoke group 234 (`test/render_scene_ktx2_budget_native.elisa`,
  logged case) on SDL3/Metal:
  - registering KTX2 textures costs no GPU bytes until a material uses them;
  - the opaque and alpha 4x4 fixtures each take more than 0 and less than the
    64 bytes an RGBA8 expansion would take, so no RGBA expansion happens;
  - releasing a texture returns its bytes;
  - a budget one byte short refuses the upload and keeps the total; a budget
    lowered below the total refuses; an exact fit is admitted;
  - budgets of 0 or above the maximum are rejected.
- `PYTHONPATH=scripts python3 scripts/render_scene_native_smoke.py` exited 0
  (ELISA_ALLOW_STALE_STAGE1=1, pinned stage1 copy).

## Negative controls (2026-10-01)

| Change | Result |
| --- | --- |
| Uploader gets the full 64 MiB limit, ignoring the scene budget | smoke exit 234, case 14 |
| Uploaded bytes are not recorded for the slot | smoke exit 234, case 6 |
| `fits` drops its `used >= budget` refusal | proof fails 6 goals; unit test panics on the postcondition |
| `remaining` tests `used > budget` instead of `>=` | survives; equivalent (`budget - used` is 0 when equal) |
- Full gate: `elisascript scripts/check.elisascript` ended with "Validation report written." on 2026-10-01.
