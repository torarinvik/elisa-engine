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

### Catch arm repair

Direct-call value catches now use the existing statement-arm emitter for
success, wildcard, generic-error and variant arms. Prefixes execute before
yields; terminating arms use the function return/error ABI; arm locals are
restored after emission. The focused catch_value_arm_prefix_smoke passes:
both outcomes, exact prefix counts, outer-binding shadow restoration and
function returns execute with status 0; an escaped arm local is rejected.
The existing catch_result_type_smoke also passes (expected exit 84).
Logs: `build/validation/catch-value-arm-prefix-smoke.log` and
`build/validation/catch-arm-result-type-smoke.log`.

The first bootstrap stopped at the default 6 GiB limit. A serialized retry
with an 8 GiB ceiling succeeded on this 24 GiB host; logs are
`build/validation/catch-value-arm-prefix-seed.log` and `-seed-8g.log`.
The repair product was built from modified compiler source based on 9c9b514c;
it is not the immutable qualified 2ad7a165 product.
Launcher rebuilding exits 2 in 3.64 seconds, peak RSS 818,224 KiB, still
with seven declines. returned_exec_status advances to its void-success
discard; report_test_setup_failure_machine advances past its bool initializer
to a later merged-error binder. Other merged-error binding/rethrow declines
remain. No executable is produced; full qualification is still open.
Artifact: `build/validation/elisascript-catch-arm-repair-build.log` and JSON.

### Void success correction

classify_returned_exec returns void on success; its legacy catch no longer
reads the absent success value in `_ = returned`. This clears that function,
leaving six output/error-binding declines. The bounded build exits 2 in
4.03 seconds, peak RSS 831,488 KiB; no executable is produced. Artifact:
`build/validation/elisascript-d67efe6e-void-success-build.log` and JSON.
The compiler product SHA is
`edbfa76ef2be47c466a3064119195b574fe6a4cb582af6788bacfeb100f6783e`.
After the compiler commit, provenance was refreshed using its existing
manifest: product SHA, source-tree and build-recipe hashes were checked
unchanged before recording source revision d67efe6e. No rebuild or source
change was hidden by that refresh. Full qualification remains open.

Remaining statement catch binders route through bind_scalar_catch_error,
which asks annotation_ident_value_type to resolve the declared family.
The failing header routines catch composite fieldless families such as
OutputContractError + OutputHumanError. A repair must preserve composite
variant offsets when comparing a bound error with a source-family variant;
declaring an arbitrary i32 binder alone is insufficient correctness evidence.

### Composite fieldless binder repair (194ba1d6)

Statement catches now bind fieldless composite families as their enum
descriptor. Equality maps source variant constants to that family's status
tag. The first binder-only attempt compiled but failed its runtime control:
both source variants had ordinal zero. Adding composite offset resolution
makes the collision control execute with status 0 at O0 and O2. Existing
catch-prefix acceptance/rejection and unknown-variant smoke checks pass.
Logs: `build/validation/composite-fieldless-catch-binder-smoke.log`,
`composite-binder-prefix-smoke.log`, `composite-binder-unknown-variant-smoke.log`.
Payload-bearing binders and generic rethrows are not covered by this repair.

The bounded launcher build now has three declines (previously six):
output_transport_planned_preflight, write_output_transport_fd, and
report_test_setup_failure_machine. Header/footer error mapping and
run_test_mode emit. Build exits 2 in 3.62 seconds, peak RSS 819,904 KiB;
no executable is produced. Artifact:
`build/validation/elisascript-composite-binder-build.log` and JSON.
The source was modified relative to d67efe6e during this qualification;
full compiler/engine qualification remains open.

### Fieldless rethrow mapping

Raising a bound fieldless error now identifies its scalar enum or composite
descriptor family and uses remap_error_status for the destination family.
Enum table identity is checked before resolving a name, so an ambiguous
same-name lookup does not silently select another family's representation.
Deferred actions and region unwinding remain on the terminating path.
The focused composite_fieldless_rethrow_smoke passes O0/O2 execution for
reversed composite family order and scalar-to-composite widening; an
incompatible destination is rejected. The composite binder smoke still
passes. Logs: `build/validation/composite-fieldless-rethrow-smoke.log` and
`fieldless-rethrow-binder-regression.log`.

Launcher rebuilding now declines two functions: write_output_transport_fd
advances past its fieldless rethrow to a payload-error binder at line 129;
report_test_setup_failure_machine still declines at its payload-error binder.
The preflight rethrow emits. Build exits 2 in 3.77 seconds, peak RSS
814,880 KiB; no executable is produced. Artifact:
`build/validation/elisascript-fieldless-rethrow-build.log` and JSON.
Payload binding/rethrow preservation and full qualification remain open.

### Acknowledgement binder correction

The previous writer diagnosis is corrected by the expression trace:
write_output_transport_fd declined Ident(state) in an `ok state:` success arm,
not a payload-error binder. Changing it to `state:` clears that function.
The bounded build now declines only report_test_setup_failure_machine at
Ident(failure), exiting 2 in 6.07 seconds, peak RSS 804,720 KiB. No executable
is produced. Artifact:
`build/validation/elisascript-ddd62fd5-ack-binding-build.log` and JSON.

The remaining generic payload-error binder requires representation conversion:
native error status stores a tag plus concatenated fields from all variants,
whereas an ordinary payload-enum value stores a tag plus word storage sized
for its largest variant. Reinterpreting the status aggregate as the value or
zeroing its payload would lose active fields. Preserve the active variant's
fields when binding; qualify payload round trips and rethrows before accepting
that compiler repair. No payload preservation claim is made here.

### Payload value round-trip reproducer

Compiler test/repro/caught_payload_value_roundtrip.elisa isolates a generic
error binder followed by matching its single-i64 and mixed-i32/i64 variants.
The current product declines only inspect at Ident(failure), before matching
or payload extraction, and writes no object. Artifact:
`build/validation/caught-payload-value-roundtrip-baseline.log`.
The intended execution checks exact values 123456 and 17 + 9001.
Reuse ordinary enum field tuple storage for the active native status variant;
retain signedness and alignment, rather than treating its concatenated fields
as the ordinary enum's largest-variant word array. Ordinary enum constructors
and match arms use zero-based tags, whereas native error status uses one-based
tags; conversion and comparisons must agree at this boundary. Existing
composite equality controls alone do not cover matching a bound error value.

### Native statement-catch payload conversion

Native statement catches now bind payload errors by copying only the active
variant's fields into ordinary enum storage. Typed single-field stores and
multi-field tuple stores preserve alignment and field widths. The stored tag
is the zero-based ordinal; composite fieldless comparison constants and
rethrow conversion now use the same representation boundary.
The caught_payload_value_roundtrip_smoke executes the exact single-i64 and
mixed-i32/i64 values at O0 and O2. Composite fieldless comparison and rethrow
smokes also pass, including their incompatible-family rejection control.
Logs: `build/validation/caught-payload-value-roundtrip-smoke.log` and
`payload-binder-composite_fieldless_{catch_binder,rethrow}_smoke.log`.
Generic value-catch payload binding and payload-bearing rethrow are separate
paths and are not established by these controls.

Launcher building reports no backend declines and reaches LLVM verification.
Verification rejects calls with incorrect argument counts, including
advance_output_transport and execute_elisascript_file_tests. Build exits 2 in
5.97 seconds, peak RSS 834,016 KiB; no executable is produced. Artifact:
`build/validation/elisascript-payload-binder-build.log` and JSON.
These call-lowering failures are the next native prerequisite; the native
gate and full toolchain qualification remain open.

### Positional error-call defaults

Named error callees now fill omitted trailing defaults before hidden arena
arguments in statement/value try, error-union calls and statement/value catch.
The focused error_call_default_arguments_smoke passes O0/O2 execution through
try and catch, with result 12; a missing required argument is rejected.
Baseline compiler 2ad7a165 instead produces invalid LLVM argument counts for
the same fixture. Logs: `build/validation/error-call-default-arguments-baseline.log`
and `error-call-default-arguments-smoke.log`.

Launcher LLVM argument-count failures are cleared. Two type mismatches remain:
the foreign kill signature, and a named selected_names argument lowered in
the RuntimeResourcePolicy positional slot. Filling a trailing default alone
does not implement named argument placement; that path remains open.
The bounded build exits 2 in 4.62 seconds, peak RSS 828,208 KiB, with no
executable. Artifact: `build/validation/elisascript-error-call-defaults-build.log`
and JSON. Full qualification remains open.

### Conflicting kill ABI and named-default reproducer

ElisaScript 4513f297 aligns the vendored crash reporter's getpid/kill
declarations with upstream i32 C scalars and converts its signal argument at
the call. The kill LLVM mismatch is cleared. The bounded launcher build now
reports only the selected_names argument in RuntimeResourcePolicy's slot;
it exits 2 in 3.76 seconds, peak RSS 826,384 KiB, with no executable.
Artifact: `build/validation/elisascript-1e64bb73-kill-abi-build.log` and JSON.

Compiler 445bbe3a adds test/repro/error_call_named_defaults.elisa. A boolean
named argument skips an i64 default, reproducing the placement problem in
one function: LLVM receives i1 true in the i64 slot and a default false in
the intended bool slot. Verification refuses emission. Artifact:
`build/validation/error-call-named-defaults-baseline.log`.
The repair must resolve supplied names to declared parameter positions before
emission and fill defaults at unsupplied positions, preserving rejection of
missing required arguments. Appending only trailing defaults is insufficient.

### Named catch placement and executable build

Statement and value catches now resolve supplied argument names against
declared parameter slots, filling defaults at omitted positions before
hidden arenas. The error_catch_named_arguments_smoke passes O0/O2 execution
for a skipped i64 default and reordered named parameters; an unknown name
is rejected. The positional default/rejection smoke still passes. Logs:
`build/validation/error-catch-named-arguments-smoke.log` and
`named-catch-positional-default-regression.log`.
Other named try and generic error-call paths are not covered by this slice.

The complete native launcher now builds with status 0 in 7.06 seconds, peak
RSS 1,033,856 KiB, within the 1.5 GiB guard. Binary:
`build/validation/elisascript-named-catch`. Artifact:
`build/validation/elisascript-named-catch-build.log` and JSON.
A bounded --help launch exits 0 and prints the CLI usage; artifact:
`build/validation/elisascript-named-catch-help.log` and JSON.
This establishes build and CLI startup only. Custom timeout/process behavior,
engine workflow execution, full compiler/prover qualification and the native
gate remain open. The installed historical launcher was not replaced.
