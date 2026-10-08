# Mesh overlay mutable-array validation

## Implemented behavior — 2026-10-08

`MeshOverlay::skin` validates inverse-bind and influence array shapes and every
positive-weight joint before clearing caller output or indexing joint matrices.
`draw` validates complete triangle topology and every index before appending.
Malformed input raises the existing `MeshError.ShapeMismatch`; public error
values, mesh ownership and valid skin/draw arithmetic are unchanged.
`SkinMeshPolicy::packed_shape_ok` uses division and remainder rather than
multiplying caller-controlled counts. The positive stride precondition and exact
shape definition remain implementation-linked contracts.

## Focused acceptance

Frozen compiler `52d60fcf` and matching runtime compile and execute
`test/mesh_overlay_shapes.elisa` at O0 and O2. Controls cover valid geometry,
truncated, empty and extra inverse binds, mismatched influence arrays, a bad
positive-weight joint in the final slot, incomplete triangles, an invalid second
triangle and the maximum u32 index. Rejections preserve seeded skin output and
leave draw output empty. Maximum-usize rows with zero slots are refused.
Logs: `build/validation/mesh-overlay-shapes-final-O{0,2}.log` and watchdog JSON;
both compiles finish in 0.36s, at 92,224 / 80,016 KiB peak RSS respectively.
The fixture is registered in `scripts/gate_tests.json`.

The emitted O0 LLVM module linked with LLVM clang using
`-O1 -g -fsanitize=address -fno-omit-frame-pointer` executes with
`ASAN_OPTIONS=halt_on_error=1:detect_leaks=1`, status 0 and no findings.
Evidence: `build/validation/mesh-overlay-shapes-asan.log`.
The frozen runtime object is not sanitizer-instrumented; this establishes the
instrumented mesh fixture path, not whole-runtime sanitizer coverage.

`python3.14 scripts/glb_skin_influence_smoke.py` passes the existing real loader
fixture at O0/O2: paired eight-slot, two-primitive normalization and malformed
attribute refusals (1.27s, 83,216 KiB, 3 GiB / 180s watchdog).
Log: `build/validation/mesh-shape-loader-regression.log`.

## Proof inventory

Verified clean prover source `452ab663df6baf307269a8ca939342180410792d`,
paired generation `cf8be5a1dc95437b8365038b580ad967`, proves and independently
replays all 211 obligations in `proof/skin_mesh_policy.elisa`. An uncached full
engine sweep retains all 73 reports and 4,277 obligations, with zero diagnostics,
failures, replay gaps or trusted assumptions. Compared with the prior inventory,
only skin policy changes: 180 to 211. All other report counts are identical.
Both product manifests and actual executable hashes were checked before the sweep.
Reports: `build/validation/mesh-shape-engine-reports/`; inventory:
`build/validation/mesh-shape-engine-inventory.json`; focused report:
`build/validation/mesh-shape-policy-final.json`.

## Remaining acceptance

The mocap consumer must integrate the clean source tuple and exercise its actual
redraw path. These controls do not identify the cause of the historical Studio
redraw crash. Full prover compatibility, compiler default grant qualification,
and renderer lifecycle memory acceptance remain open.
