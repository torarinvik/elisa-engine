# Strict global-grant entrypoint census — 2026-10-09

The 68 local-grant findings across 25 native, probe and example source files
were repaired at their individual call sites. Those callers now name the
`Global.Read` / `Global.Write` capabilities explicitly; constructor and
`catch` calls use the same narrow local grants. The repair also exposed a
separate move error in `Playback::tick`: a mutable `AnimState::clip` was passed
by value to `clip_length`. A private helper now reads through the state
reference, preserving the public `clip_length(Clip)` API and the clip value.

The census is now a preflight in `scripts/run_tests.py`. The helper discovers
top-level `main` entrypoints outside the 216-test manifest, excludes negative
fixtures, and explicitly includes the Maze C-ABI entrypoint. Each source is
compiled without `-permissive` through a wrapper that includes
`src/runtime/public.elisa` before its entrypoint. All 89 pass strict
`-emit check` on installed Stage1 SHA256
`1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24`; the
compiler hash was unchanged. Source and recursive include-closure hashes,
along with per-entrypoint results, are in
the product-pinned report
`build/validation/global-grant-entrypoint-qualification-1505c598.json`.

The standard uncached gate, now including both grant preflights,
`python3 scripts/run_tests.py /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 -j 2 --no-cache`
passes the 42/42 CLI controls, 89/89 strict entrypoints and all 216 engine tests
in 73 seconds. The grant harness and runner integration unit suite passes
20/20, including inventory selection, the runtime wrapper, product and source
closure pinning, and failure-before-engine behavior. A focused normal-runner invocation also
rebuilds and runs `anim-state` successfully. The inferred-row compiler helper,
proof pair, and fresh Course and Studio consumer builds remain open. Rerun
these checks on the newer compiler candidate after its self-host and parity
qualification completes.
