# Strict global-grant entrypoint census — 2026-10-09

The 68 local-grant findings across 25 native, probe and example source files
were repaired at their individual call sites. Those callers now name the
`Global.Read` / `Global.Write` capabilities explicitly; constructor and
`catch` calls use the same narrow local grants. The repair also exposed a
separate move error in `Playback::tick`: a mutable `AnimState::clip` was passed
by value to `clip_length`. A private helper now reads through the state
reference, preserving the public `clip_length(Clip)` API and the clip value.

The previously inventoried 89 entrypoints outside the 216-test manifest were
recompiled without `-permissive`, each through a wrapper that includes
`src/runtime/public.elisa` before its entrypoint. All 89 pass strict
`-emit check` on installed Stage1 SHA256
`1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24`; the
compiler hash was unchanged. Source hashes and per-entrypoint results are in
the ignored report
`build/validation/global-grant-entrypoint-census-wrapper-1505c598.json`.

After these source changes, the standard uncached gate
`python3 scripts/run_tests.py /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 -j 2 --no-cache`
passes all 216 tests with the 42/42 grant preflight, in 30 seconds. A focused
normal-runner invocation also rebuilds and runs `anim-state` successfully. The
89 wrapper checks establish strict semantic compilation; native behavior is
covered by the engine gate where applicable. The inferred-row compiler helper,
proof pair, and fresh Course and Studio consumer builds remain open. Rerun
these checks on the newer compiler candidate after its self-host and parity
qualification completes.
