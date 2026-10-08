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

## Length-preserving permanent-copy control

Inspected check_local_view_return_escape: by-value dstr locals are classified
as inferred-region roots without tracing alloc_perm through ctx_fstr_alloc.
A standalone control copies a local three-byte dstr through string_view_copy
and constructs a counted view with unsafe_sview_bounded_bytes. Compilation and
execution pass, retaining x/NUL/y (three bytes, including the interior NUL).
This validates that boundary, not the launcher. An earlier control using
ctx_fstr_alloc/ctx_fstr_append failed the length assertion and needs separate
investigation before interpreter return sites are changed. No interpreter or
compiler source changes made in this investigation.
Artifact: `build/validation/contract-error-marker/permanent_view.elisa`;
compile log: `permanent-view-copy-direct.log` in the same directory.

## Decoded text permanent backing

Decoded text now returns through permanent_text_copy: string_view_copy provides
permanent backing, and a locally audited counted-view construction preserves
embedded NUL bytes. The earlier append control discrepancy resolves by retaining
length before the by-value bytes_view call; implicit and explicit append calls
both pass, as does the returned three-byte x/NUL/y control. The bounded launcher
compile clears one lifetime error (11 to 10), exits 1 elsewhere in 3.02 seconds,
peak sampled RSS 726,896 KiB. Artifact:
`build/validation/elisascript-2ad7a165-permanent-copy-build.log` and JSON.
This introduces an additional copy for longer strings; launcher behavior remains
runtime-unverified, and no escape-check suppression is used.

## Semantic clearance and backend emission

Ten remaining text/path returns now copy validated counted views into permanent
backing, preserving exact length and embedded NULs through permanent_text_copy.
All semantic diagnostics clear. The bounded build reaches backend emission but
exits 2 with 13 declined functions; no linkable object/executable is produced.
Artifact: `build/validation/elisascript-2ad7a165-return-copies-build.log` and JSON.
These copies add allocation/work for longer strings. Launcher runtime behavior
and backend compatibility remain unverified.

Declined functions: output_render_record_bytes, output_render_header_bytes,
output_render_footer_bytes, output_transport_planned_preflight,
elisascript_posix_waitpid, write_output_transport_fd, python_text_decode_next,
decode_utf8, split_lines, trim_unicode_whitespace, returned_exec_status,
report_test_setup_failure_machine and run_test_mode. This is a new backend
failure family exposed after semantic repairs; it is not a successful launcher.

## Backend string lengths and catch binders

The backend trace identifies unsupported len(sview) calls in four Python text
helpers. These now use sview_len. Output catches bind successful values directly
(value/total/payload), replacing ok-prefixed binders. Four Python text functions
and output_render_record_bytes now emit: declines fall from 13 to 8. Other
output catches advance to failure-binding/expression-statement declines; their
error mapping is unchanged. The bounded build exits 2 in 5.58 seconds, peak
sampled RSS 821,360 KiB; no executable is produced. Artifact:
`build/validation/elisascript-2ad7a165-backend-forms-build.log` and JSON.
Python text and report behavior remain runtime-unverified.

## Native wait-status reference

ElisaScript commit `d2f97ef6` passes `&native_status` to the native waitpid
mutable i32 reference. C writes four bytes into the local; the wrapper widens
the result only when a child is reported. The bounded build clears this
backend decline, leaving seven catch/error declines. It exits 2 normally in
3.85 seconds with peak sampled RSS 808,192 KiB; no object or executable is
produced. Artifact: `build/validation/elisascript-2ad7a165-waitpid-build.log`
and JSON. Wait/timeout behavior and the full native gate remain unverified.

## Value-catch arm prefix reproducer

`../Elisa-compiler/test/repro/catch_value_arm_prefix.elisa` isolates a valid
return-position catch with a discard statement before each scalar yield.
The qualified compiler exits 2 and declines only select_value at the Catch
expression; no object is written. Artifact:
`build/validation/catch-value-arm-prefix-2ad7a165.log` and JSON.
The direct-call catch emitter uses arm_value_expression, which accepts exactly
one expression statement. This explains the prefixed success arm in
returned_exec_status and the prefixed error arm in report_test_setup_failure_machine.
The repair should reuse emit_arm_body_into_slot for statement prefixes and
terminating paths, retaining arm-local scope and the error status/payload ABI.
Generic merged-error binding/rethrow declines require separate coverage.
