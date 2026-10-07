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
