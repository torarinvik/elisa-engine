# Per-instance tints

A snapshot row can carry a `RenderSnapshot::Tint`: an RGB color multiplier and
an emissive RGB multiplier with a strength. The tint changes one rendered
instance without creating or mutating a material, so instances that share a
mesh/material pair stay shared.

## Contract

| Field | Range | Wicked target |
| --- | --- | --- |
| `color` | each channel finite, 0 to 4 | `ObjectComponent::color`, multiplied into the material base color |
| `emissive` | each channel finite, 0 to 1 | `ObjectComponent::emissiveColor.xyz` |
| `emissive_strength` | finite, 0 to 64 | `ObjectComponent::emissiveColor.w` |

- `tint_neutral()` (all ones) renders exactly as the material.
- Wicked computes `material.GetEmissive() * inst.GetEmissive()`, so the
  emissive multiplier scales authored emission. A material with no emission
  stays dark.
- Alpha is not tinted. Wicked does not move an object into the transparent
  pass for instance alpha, so the instance alpha stays 1.
- `WorldRendering::set_tint` sets the tint on one existing binding. It raises
  `InvalidTint` for a tint outside the ranges above, including NaN or infinity.
  It raises `UnknownBinding` when the gameplay entity and render ID are not
  bound, and leaves the previous tint in place on either error. Rebinding a
  render ID keeps its tint; a new binding starts neutral.
- `extract` copies each row's tint into the snapshot through
  `RenderSnapshot::snapshot_bind_tinted`. `snapshot_bind` resets the tint to
  neutral. Both reject an invalid tint, and `snapshot_valid` and
  `bindings_valid` check every row's tint. All public write paths validate
  first, so those two row checks are defense in depth that tests cannot reach
  through the public API.

## Native path

The presenter stages each row as before. For a row with a non-neutral tint it
then calls `elisa_render_scene_v1_snapshot_stage_tint(render_id, ...)`, so a
neutral row costs no extra ABI traffic. The native call rejects these cases
with `INVALID_ARGUMENT`:

- no active transaction
- a render ID that no staged row has
- a second tint for the same row
- any value outside the table above

Like other staging errors, a rejection leaves the transaction open for the
presenter to abort. Commit writes the staged tint, or the neutral default, to
the `ObjectComponent` of every committed row, so dropping an override resets
the instance. Retained rows must still have an `ObjectComponent` at commit, or
the transaction fails with `UNKNOWN_HANDLE` before any change.

## Evidence

Portable tests:

- `test/world_rendering.elisa` codes 25–32:
  - Default tints are neutral.
  - A tint survives rebinding and reaches the snapshot row it belongs to, while
    the other row stays neutral.
  - NaN, infinite, negative and over-range colors are rejected, as are
    over-range emission and NaN or over-range strength.
  - An unknown entity or render ID is rejected.
- `test/render_snapshot.elisa` codes 14–18:
  - `snapshot_bind_tinted` updates an existing row.
  - An all-zero tint is valid but not neutral.
  - A negative strength is rejected without adding a row.
  - A neutral rebind resets the tint.

The SDL3/Metal smoke tints one of the 200 stress instances that share one
mesh/material pair (`test/render_scene_snapshot_native.elisa`):

| Code | Check |
| --- | --- |
| 95 | `set_tint`, extract and sync succeed |
| 96 | the transaction makes 203 native calls: 202 plus one tint |
| 97 | the tinted instance's `ObjectComponent` holds the tint; its neighbors are neutral |
| 98 | one shared mesh remains; the tinted instance and a neighbor still share it and report the unchanged material factors |
| 99 | the direct-ABI rejections below create no objects or instances |
| 101–111 | direct ABI: tint outside a transaction, before its row is staged, NaN/infinite/negative/over-range color, over-range/negative/NaN strength, over-range emission, render ID 0, and a duplicate are rejected; one valid tint is accepted; abort succeeds |
| 113–115 | resetting to neutral restores the neutral multipliers and 202 calls |

The probe reads the live `ObjectComponent`, not rendered pixels.

Mutation checks, each restored afterwards:

| Mutation | Exit |
| --- | --- |
| commit does not write `color` | 97 |
| commit writes `color` only for tinted rows (no reset) | 115 |
| a duplicate tint is accepted | 110 |
| the color range is not checked | 105 |
| `snapshot_bind_tinted` skips validation | 17 |
| a same-pair update keeps the old tint | 15 |
| `set_tint` stores nothing | 31 |
| `extract` drops the tint | 31 |

## Maze client

While the player carries the key, `MazePresentation::scene_update` gives the
player marker a warm color tint (`key_carrier_tint`). It restores the neutral
tint when the key is gone after a restart. The player keeps its authored
material; the tint is color-only because that material has no emission to
scale.

- `examples/maze/main.elisa` checks that the tint appears in the snapshot after
  the win and clears after restart (codes 36–38).
- `test/maze_rendering_native.elisa` checks the live instance: the player is
  tinted, the door is neutral, and the player is neutral again after restart
  (codes 22–23).
- Mutations that never apply the tint, or never clear it, exit 37 and 38.

## Limits

- Tints cover color and emission only. There are no per-instance roughness,
  metalness, texture or alpha overrides.
- The native checks cover static shared-mesh instances. Skinned snapshot
  instances go through the same commit loop but have no dedicated test.
- There is no pixel-level check that the GPU applies the half-float packed
  values.

## Validation on 2026-09-21

- Passed: the focused `render_snapshot` executable (including
  `WorldRenderingValidation`) and `examples/maze/main.elisa`.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal, including the
  snapshot stress batch and the maze world.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17/17 and 6/6).
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
