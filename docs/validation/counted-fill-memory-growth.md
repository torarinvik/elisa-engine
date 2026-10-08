# Counted-fill memory regression — 2026-10-08

## Current result

Frozen compiler `cb10dd72c8e760198f1471636bde26b27d579e95`, product
SHA-256 `2809fb15a2d6e95cebfee520f62ae67aae2c6c45a9ad76bdd81b7d44ccbc4ece`,
cannot yet qualify the engine runtime suite. Both uncached parallel and serial
runs exceeded their RSS watchdogs. An isolated `viewport_mesh` reproduction
identifies repeated exact-size pre-reservation while reading the real 53 MiB
boxer GLB, before mesh parsing. This is separate evidence from the mocap client's
older sealed skin-cache crash; their causes have not been equated.

## Evidence

Retained under `build/validation/`:

- `viewport-mesh-sizes-run.log`: temporary instrumented O0 reproduction writes
  count/capacity after each chunk. Both grow by 65,536 each time. The watchdog
  observes 1,507,008 KiB RSS after about 14 MiB of input and terminates the child.
- `viewport-mesh-no-reserve-build.log` and `viewport-mesh-no-reserve-run.log`:
  compile the unchanged production `test/viewport_mesh.elisa` with only
  `ELISA_DISABLE_MEMORY_RESERVE=1`, using the same frozen compiler/runtime.
  Runtime passes in 0.72s, peak 301,792 KiB, with 80,964 vertices, 90,240 faces,
  zero rest-pose drift and 24,602 inked pixels. The runtime cap remains 524,288 KiB.
- `counted-fill-growth.elisa` and `counted-fill-before-{build,run}.log`:
  a nested counted-fill reproducer compiles at O2, then returns 1 when a later
  capacity increase is smaller than twice the previous capacity. This isolates
  allocation growth without asset parsing or the engine's mesh APIs.
- `counted-fill-growth-evidence.json`: exact commands, terminal statuses,
  compiler/runtime hashes and the explicit unimplemented-fix state.

The failed runtime processes are terminal watchdog terminations. An interrupted
compile also left an empty unrelated test executable; its execution error is
not evidence of a source-level regression.

## Repair scope

`codegen_loop_prereserve.elisa` inserts an exact-size `emit_darray_reserve` before
an inner fill. Repeated outer iterations therefore request one additional chunk
at a time. `arena_realloc` retains moved backing storage because copied array
headers can still refer to it. The interaction produces quadratic retained
storage when blocks must relocate.

Compiler commit `04761c6862cdec8fb8c47d0d1f2f5f47d6f9b618` in isolated
`../Elisa-compiler-counted-fill-fix` routes only
compiler-inserted reserves through the existing geometric darray growth helper.
Explicit user reserve keeps its current behavior. The isolated product
build passes, while compiler main remains unchanged for the running native gate.
Focused qualification passes the expanded nested-fill controls at O0/O2, explicit
reserve/no-shrink and scalar-fill controls, and the existing loop-prereserve
fixture. The original uncached runtime sweep is now running on a frozen product
and matching rebuilt runtime; all 215 tests pass uncached in 25.54s,
peak 1,303,936 KiB under the original 3,145,728 KiB aggregate cap.
Retain existing memory limits and source assertions. Do not install a replacement
product or claim full native/prover compatibility from the ablation result.

## Fixed product evidence

Product SHA-256
`49c58a6128a48f44a85584a4fc3ca78b3fd0849f4ffab1f95f0262854a3b8688`.
Frozen copies are `stage1-code-04761c68` and `runtime-04761c68.o`.

The unchanged real-boxer test passes with the automatic reserve pass enabled:

| Build | Elapsed | Peak RSS | Runtime cap |
| --- | --- | --- | --- |
| O2 | 0.53s | 155,808 KiB | 524,288 KiB |
| O0 | 1.07s | 292,752 KiB | 524,288 KiB |

Both retain 90,240 drawn faces, zero rest-pose drift and 24,602 inked pixels.
`viewport-mesh-sizes-fixed-run.log` retains the exact post-fix count/capacity
sequence: capacity grows geometrically to 67,108,864 for 55,871,924 input bytes.
It completes parsing, skinning and rendering under the same 524,288 KiB cap.
The pre-fix sequence is preserved in `viewport-mesh-sizes-run.log`.
Focused runs use the original runtime to isolate the compiler change; the full
runtime sweep uses the newly built matching runtime. No disable-reserve override
is used in fixed-product qualification.

`compiler-04761c68-runtime.log` and its watchdog JSON retain terminal status 0:
215 total, zero cached compiles, zero remote compiles. This restores runtime
qualification; full prover compatibility and native acceptance of this product
remain open. The concurrent native gate uses the earlier cb10dd72 product.
