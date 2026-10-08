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

## Removal and copy host paths

Removal, same-file-safe copying and recursive removal now bind validated,
NUL-terminated host paths as cstr with local conversion grants. Buffer lifetimes,
identity checks and traversal/depth checks remain. Six prior conversion
rows clear in the bounded compile, which exits 1 elsewhere in 3.72 seconds,
peak sampled RSS 726,880 KiB. Artifact:
`build/validation/elisascript-2ad7a165-remove-copy-cstr-build.log` and JSON.
Removal and copy behavior remain runtime-unverified on this compiler.

The initial paired prover build refused a revision mismatch: the compiler seed
preceded committing its patch and still recorded ddbc803d. Verified unchanged
product SHA, then used stage1_provenance.record with the seed manifest as the
expected input fingerprint; exact source-tree and build-recipe hashes matched.
The manifest now records committed 2ad7a165 and was copied into the snapshot.
No compiler binary or source was changed by this provenance refresh.

## Recursive copy and canonical path strings

Recursive copying and canonical-path resolution now bind validated terminated
paths as cstr before host calls. Copy traversal bounds, symlink handling and
realpath result validation/freeing remain. Three prior conversion diagnostic
rows clear; bounded build exits 1 elsewhere in 5.13 seconds, peak sampled RSS
726,800 KiB. Artifact:
`build/validation/elisascript-2ad7a165-copytree-realpath-build.log` and JSON.
Recursive copy and canonical-path behavior remain runtime-unverified.

## Process argv string boundaries

All five process-launch paths bind executable and argument C strings once after
text validation, NUL-terminated buffer construction and null checks. The owner
array finishes growing before argv publishes pointers; the final NULL slot and
host-call scope remain. Pointer-conversion diagnostics drop from 35 to 15,
clearing 20 rows. Bounded build exits 1 elsewhere in 3.46 seconds, peak sampled
RSS 726,688 KiB. Artifact:
`build/validation/elisascript-2ad7a165-process-argv-cstr-build.log` and JSON.
Process launch/replace/capture behavior remains runtime-unverified.

## Paired prover and engine proof sweep

The paired build passes after the verified provenance refresh, producing
generation e9438588a1394963bfc4ecc3223c0466. The uncached engine proof sweep
passes 73/73, status 0, on compiler 2ad7a165 and prover 3e1a6c50 (production
source remains 95db5c6b). Logs:
`build/validation/proof-3e1a6c50-2ad7a165-build-final.log` and
`build/validation/proof-3e1a6c50-2ad7a165-engine-proofs.log`.
Full regression matrix, shared and native gates remain open.

## Environment and directory C strings

Environment access/mutation and directory operations bind validated terminated
names, values and paths as cstr. The child-process working-directory conversion
is local to its checked non-null branch, backed by the existing owned buffer.
All 15 remaining pointer-conversion diagnostics clear. Bounded build exits 1
with only 11 interpreter return-lifetime diagnostics in 4.93 seconds, peak
sampled RSS 727,504 KiB. Artifact:
`build/validation/elisascript-2ad7a165-env-directory-cstr-build.log` and JSON.
Environment/directory behavior remains runtime-unverified on this compiler.

## Permanent runtime view lifetime investigation

The 11 remaining interpreter errors include views of ctx_fstr_alloc payloads
(allocated by alloc_perm) and Fs helpers explicitly passed perm_arena. Tried
an in perm scope on one decoded-text return: all 11 diagnostics remain. Adding
@perm likewise does not clear the launcher error; a minimized standalone case
also rejects perm as an unknown region qualifier. Reverted the unsuccessful
interpreter changes. Reproducer/log:
`build/validation/contract-error-marker/permanent_view.elisa` and
`build/validation/contract-error-marker/permanent-view.log`.
The permanent payload semantics need an accurate compiler/runtime lifetime
boundary; no alias or escape suppression has been added. This finding does not
qualify the launcher or show the lifetime errors are all false positives.
