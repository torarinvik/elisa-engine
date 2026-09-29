# 2D preset CPU timing

Measured on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is P08 progress; `world2d-presets.md` listed preset CPU
timing as a gap.

## Method

The engine has no wall-clock API, so each preset was timed from outside the
program.

- A throwaway executable per preset repeats `settle` from
  `test/physics_world2d_presets.elisa` 2000 times. Each run is 188 frames of
  16 ms on the five-ball tower.
- Each executable was run 5 times and the minimum wall time was kept.
- The executables were not committed.

## Result

| Preset | Substeps × relax passes | Total | Per frame |
| --- | --- | --- | --- |
| fast | 1 × 4 | 12.15 s | 32.3 µs |
| balanced | 4 × 2 | 11.74 s | 31.2 µs |
| stable | 8 × 1 | 12.30 s | 32.7 µs |

## Reading

- The three presets cost the same to within 5%, even though stable runs 8
  substeps per frame and fast runs 1.
- So, for a five-body scene, the cost per frame is dominated by fixed
  overhead and not by the solver.
- The likely causes are copying the 32-slot body array by value and the
  per-frame overlap scan in the harness. Neither was profiled.
- On this scene, any preset costs well under 0.1 ms per 60 Hz frame.
- These numbers are not a solver-cost ranking. They cannot guide choosing a
  preset for large scenes.

## 32 bodies

A second throwaway benchmark separates the harness overhead from the solver.

- It places 32 balls in a 4 × 8 grid and calls `step_frame` on 20,000
  frames of 16 ms, with no per-frame scan.
- A baseline with the same loop and no `step_frame` call costs 0.19 µs per
  frame, so this measures the solver.

| Preset | Per frame | Share of a 16.7 ms frame |
| --- | --- | --- |
| fast | 125 µs | 0.7% |
| balanced | 149 µs | 0.9% |
| stable | 188 µs | 1.1% |

- Here the ranking matches the preset names: stable costs 1.5× fast.
- It grows much less than the substep count (8×), so the relax passes and
  the fixed per-frame work, not the substeps, dominate at this size.
- At the 32-body cap every preset fits comfortably in a 60 Hz frame on an M5.

## Gaps

- Pair checks are O(n²) and bodies are capped at 32. Larger scenes need a
  broadphase before they can be timed.
- There is no in-engine timer, so this timing cannot run in the gate.
- Presets for the walkable world and for 3D remain open.
