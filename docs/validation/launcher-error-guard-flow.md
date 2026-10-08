# Launcher error guards and gate restoration

Date: 2026-10-08. Qualification remains incomplete.

The ElisaScript recovery lowerer now discards error requirements introduced by
the guarded expression before lowering its fallback. Previously, recovered
stderr writes appeared in the helper's escaping error row. The verifier change
tracks guard stacks across normal and recovery edges, rejects incompatible
joins and empty pops, and covers whole-family requirements only with unfiltered
guards. A filtered guard may cover an explicitly raised matching operation.
Effects remain independently required.

## Bounded evidence

The launcher `build/validation/elisascript-guard-flow` built with compiler
`84320c8a`, Script baseline `f2bd4fea` and uncommitted guard changes. Its SHA256
is `a7183840025ed9d1c640664c4dadf154570b376a852bbf964441fbd4e602e02c`.

- `guard-flow-recovered.log`: recovered stderr helper executes, status 0.
- `guard-flow-unrecovered.log`: unguarded helper rejected, status 1.
- `guard-flow-fallback-error.log`: error from fallback remains escaping and
  is rejected at its caller, status 1.
- `guard-flow-native-quick-pinned.log`: quick gate status 0, 0.73 seconds.
- `guard-flow-native-headless.log`: headless gate status 0, 43.26 seconds,
  peak RSS 784,944 KiB. Boundary ASan/UBSan, navigation, audio stream and
  lifecycle ASan/UBSan and TSan, and worker TSan checks passed.

All logs above are under `build/validation/` with adjacent JSON watchdog
reports. Gate invocations pin PYTHON_BIN to `/opt/homebrew/bin/python3.14`
and ELISASCRIPT_BIN to the tested launcher. The initial unpinned quick report
identified another PATH launcher; use only the explicitly pinned evidence.
Quick and headless runs skip application and rendered native stages.

## Compiler prerequisite exposed by the adversarial control

`../elisa-script/test/repro/error_guard_row_flow.elisa` tests an unfiltered
guard, different-family guard, pop-before-raise, fallback raise, reordered
cross-block coverage, incompatible joins and empty pops. The cross-block
positive control fails before the compiler repair. It must pass before commit
or installation of the verifier change.

Generated LLVM in `build/validation/guard-flow-controls.ll` shows
`worklist.pop().usize()` storing its i32 pop result into an i64 alloca, then
loading i64 without extension. Uninitialized high bits can cause reachable
blocks to be skipped. A direct helper call happens to pass, so that control
alone would give insufficient evidence. Compiler reproducer:
`../Elisa-compiler/test/repro/darray_pop_numeric_conversion.elisa`.

The proposed compiler repair gives darray pop its declared element type in
expression typing, preserving width and signedness for subsequent numeric
conversion. Bootstrap artifact: `build/validation/pop-conversion-seed.log`.
Compiler fix `cb10dd72` builds successfully; its product SHA256 is
`2809fb15a2d6e95cebfee520f62ae67aae2c6c45a9ad76bdd81b7d44ccbc4ece`.
The numeric conversion reproducer builds and executes with status 0 at O0
and O2 (`pop-conversion-o{0,2}-{build,run}.log`). The emitted LLVM now has
zext i32→i64, sext i32→i64 and zext i8→i64 for the three controls;
`pop-conversion-fixed.ll` retains this evidence. The pre-fix excerpt is
`pop-conversion-before.ll`. An invalid text-pop-to-u64 control is refused with
status 2 by LLVM verification; it is not a semantic-diagnostic pass.

Script fix `62928532` passes all seven focused guard controls with the rebuilt
compiler (`guard-flow-controls-fixed-{build,run}.log`, both status 0).
The fresh launcher `build/validation/elisascript-guard-flow-qualified` builds
with peak RSS 1,035,104 KiB in 6.44 seconds. Its SHA256 is
`8edba764b397aac70a2a39b943edce4faee9be6ee3c8143f7f4341ca3fbc2128`.
It executes the recovered helper and rejects both unguarded helper and
escaping fallback (`guard-flow-qualified-{recovered,unrecovered-rejected,fallback-rejected}.log`).
The pinned fresh headless replay passes in 39.49 seconds, peak RSS 784,944 KiB
(`guard-flow-qualified-headless.log` and watchdog JSON). The saved gate report
`guard-flow-qualified-headless-report.json` records the exact launcher hash,
headless status 0 and hardware verification unverified. Application and native
rendered stages are skipped in this mode.

Full compiler runtime/prover matrix and rendered native gate remain open.
Filtered guards with nonzero operation identities remain conservatively
unqualified; the focused filtered control uses legacy zero identity.
These checks contain no new engine proof obligations.
