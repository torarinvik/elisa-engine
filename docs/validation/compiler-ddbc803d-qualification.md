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
