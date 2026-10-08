# Compiler 2ad7a165 qualification — 2026-10-08

Immutable snapshot: build/validation/stage1-code-2ad7a165.
Source: 2ad7a16563d0429f8f2963a78e88c7fbc5d9474c.
Compiler SHA-256: b275e9e0980c08f411935d169eed97153a4c143eef6fcd232a37aa029bbc2d60.

The focused contract-marker propagation controls pass. The uncached engine
runtime suite passes: 215 total, 0 cached compiles, status 0, 74 seconds, with
audio forced unavailable. Log: build/validation/compiler-2ad7a165-runtime.log.
This elapsed time is not a controlled compiler performance comparison.
Prover, shared and native qualification on this compiler remain open.

## Caller storage region repair

Three source/file runner helpers now name caller storage's region @r and append
argv RuntimeValues inside in r, while temporary lowering remains in its bounded
source region. Signature annotations alone retained the errors; explicitly
selecting r clears all three, with no runner diagnostics in the final bounded
compile. It exits 1 elsewhere in 6.51 seconds, peak sampled RSS 726,832 KiB.
Artifact: `build/validation/elisascript-2ad7a165-runner-region-owned-build.log`
and JSON. Runner storage survival remains runtime-unverified on this compiler.

## Symlink host string boundaries

Symlink probing, readlink input and symlink creation now bind cstr after path
validation, terminated-buffer construction and null checks. Owners stay in the
enclosing function through the host call; readlink's output remains bounded
raw bytes. Three prior pointer-conversion diagnostic rows clear in the bounded
compile, which exits 1 elsewhere in 9.05 seconds, peak sampled RSS 726,768 KiB.
Artifact: `build/validation/elisascript-2ad7a165-symlink-cstr-build.log` and JSON.
Symlink behavior and escaping readlink-result lifetime remain runtime-unverified.
