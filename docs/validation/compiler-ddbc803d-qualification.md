# Compiler ddbc803d qualification — 2026-10-08

## Immutable product and runtime

The installed compiler and the UI consumer's retained snapshot identify source
`ddbc803dd65aad3a5e5515f432d40702ae62124c`. Copied the consumer snapshot into
`build/validation/stage1-code-ddbc803d` with timestamps preserved. Product hashes:

- Compiler: `b4439c0972f53fbc3738ddc25f58a713e9cc4535a9142253baf9eda0483d464b`.
- Runtime: `ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.

The first plain copy changed source mtimes and correctly triggered the compiler's
freshness refusal before compilation. Repeated the copy with `cp -pR`; no stale
product override was used. Initial refusal evidence is retained in
`build/validation/compiler-ddbc803d-runtime.log` and
`compiler-ddbc803d-copy-timestamp-refusal.json`.

## Runtime acceptance

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
/opt/homebrew/bin/python3.14 scripts/run_tests.py \
  "$PWD/build/validation/stage1-code-ddbc803d/scripts/elisac_stage1.sh" --no-cache -j2
```

**215 total, zero cached compiles, status 0**, 104 seconds. Log:
`build/validation/compiler-ddbc803d-runtime-preserved.log`. This covers the
engine runtime manifest; physical audio and full shared/native qualification
remain open. Concurrent host work means the elapsed time is not a compiler
performance comparison.

## Prover and remaining gates

The complete 73-report engine proof sweep is established on the prior immutable
8006 product with prover `cb316eaa`; see
[enum summary evidence](reference-free-enum-summary.md). Building and qualifying
a paired prover on ddbc803d, the full prover matrix, shared/native gates and
hosted pins remain required before adopting this tuple for those gates.

## Paired prover acceptance

Built committed prover `f3ee9522` with the immutable ddbc803d product, matching
runtime and compiler parser snapshot. Both strict O2 products build successfully
as clean generation `ac70e531389c4462b0906fdd9b74e5e5`.

- Prover SHA256: `2b9c80f7934150f9fde494ead7d561e28581957db2ee3e6dfdc2dba555bd19b6`.
- Replay SHA256: `f71eaaf7ae47b48161c37c03d66a01db2d1d289e5db919da779fe5ace500d68b`.
- Build log: `build/validation/proof-ddbc803d-paired-build.log`.
- `scripts/prove_all.py --no-cache -j2`: **73 total, zero cached, status 0**.
  Log: `build/validation/proof-ddbc803d-engine-sweep.log`.
- `test_loop_invariants_compile.py` with compiler binary/root/revision explicitly
  set to the immutable ddbc snapshot: compiled source-bound replay controls pass;
  original loop fixture proved with zero replay gaps. Log:
  `build/validation/proof-ddbc803d-source-binding-controls.log`.

This supersedes the paired-build prerequisite above. The complete compatibility
matrix still needs its remaining failures repaired and a fresh run on this
product; the retained old-product matrix ended with 73 failed steps. Shared and
native gates, hosted pins and physical hardware checks remain open.

## Native launcher prerequisite — bounded attempt

On 2026-10-08, compiled ElisaScript checkout `36a3374a` directly with the
immutable ddbc803d wrapper at O0, using the engine watchdog's 180-second and
1,572,864 KiB RSS limits. The attempt terminated normally with status 1 after
5.85 seconds; peak sampled RSS was 728,880 KiB. No launcher was installed.

The compiler rejects the script checkout's vendored runtime: assignments to
immutable `exponent_marker` and `decimal_marker` at lines 43–44, and `cstr`
returns receiving references starting at line 247. These source compatibility
errors must be resolved before qualifying the launcher required for explicit
native process deadlines. This attempt does not qualify the shared/native gate.

Retained artifacts: `build/validation/elisascript-ddbc803d-build.log` and its
`.log.json` watchdog report. The script validation hold and override environment
were left unchanged; this was the bounded engine integration build.

### Targeted launcher repairs — 2026-10-08

ElisaScript now builds owned argument storage before publishing pointers in
all five process paths. The bounded before/after attempts remove all 25
`argv` storage-dependency invalidation diagnostics. A subsequent repair
restores the absent ASCII whitespace helper (space or bytes 9–13), clearing
four undefined-identifier diagnostics. Argument order and NUL validation
remain intact; process execution has not been verified because compilation
still fails on other compatibility errors.

The latest attempt exits 1 normally in 8.65 seconds, sampled peak RSS
726,880 KiB under the same 180-second / 1,572,864 KiB limits. Artifacts:
`build/validation/elisascript-ddbc803d-argv-fixed-build.log` and
`build/validation/elisascript-ddbc803d-text-helper-build.log`, each with a
watchdog JSON report. No installed launcher was replaced.

The next targeted repair uses a tuple-valued loop for the vendored float
formatter's exponent/decimal marker scan. It preserves last-match and absent
marker values and clears both immutable-assignment diagnostics without adding
mutable outer locals. The bounded build completes with status 1 on unrelated
errors in 5.98 seconds, sampled peak RSS 726,896 KiB. Artifact:
`build/validation/elisascript-ddbc803d-marker-loop-build.log` and watchdog JSON.
The formatter has not executed on this compiler; full launcher qualification
remains open.
