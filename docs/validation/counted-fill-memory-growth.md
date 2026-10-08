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

The prepared `build/validation/counted-fill-geometric-growth.patch` routes only
compiler-inserted reserves through the existing geometric darray growth helper.
Explicit user reserve keeps its current behavior. The patch passes `git apply
--check`; it has not been applied or qualified while the full native gate uses
the current product. Qualification must cover the nested-fill reproducer at
O0/O2, the unchanged real-boxer test, and the original uncached runtime sweep.
Retain existing memory limits and source assertions. Do not install a replacement
product or claim full native/prover compatibility from the ablation result.
