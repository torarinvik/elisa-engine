# 3D stacked bodies and large timesteps

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler and the Wicked/Jolt backend. This is P08 progress: the done
criterion lists stacked bodies and large timesteps, and until now only the
2D path had regression budgets for them.

## Test

`test/physics_stack3d_probe.elisa` runs in the same native smoke as the
thin-wall test (`physics-ccd3d-smoke`).

- It stacks five 0.5 m, 10 kg boxes with 1 cm gaps on a static floor.
- It steps the stack for 5 s in two runs: 300 steps at 60 Hz, then 75 steps
  at 15 Hz.
- It measures the drift of the top box from its ideal resting centre at
  2.25 m, as |dy| + |dx| + |dz|.

## Measurements

| Step rate | Measured drift | Budget | Failure code |
| --- | --- | --- | --- |
| 60 Hz | 3 mm | 10 mm | 30 |
| 15 Hz | 39 mm | 80 mm | 31 |

A tower that topples or sinks would miss by hundreds of millimetres.

## Checks

- `scripts/application_native_smoke.py --only physics-ccd3d-smoke` passes.
- Negative control: widening the gaps to 0.6 m, so the tower cannot rest
  where it is expected to, fails the smoke with status 30.
- The source-length check passes.

## Gaps

- The engine does not expose Jolt's solver iteration counts, so there are no
  3D solver presets. Only Jolt's defaults are measured here.
- Rotated stacks, pyramids and mixed masses are not tested.
