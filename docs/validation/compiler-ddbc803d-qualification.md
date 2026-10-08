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

A focused upstream-compatible return-type correction declares all three
static-literal boolean string helpers as `cstr`; their implementation still
returns `"True"`/`"False"`. Both boolean forwarding-wrapper type errors clear,
with no pointer casts added. The bounded attempt completes with status 1 on
remaining errors (6.24 seconds; sampled peak RSS 726,896 KiB). Artifact:
`build/validation/elisascript-ddbc803d-bool-cstr-build.log` and watchdog JSON.
This is compile-diagnostic evidence, not an executed launcher.

Integer formatting now preserves `cstr` across its three helpers and checks
that the writing `snprintf` matches its measured length before returning
interned or arena storage. Both integer wrapper errors clear and the changed
prelude helper has no diagnostics. The bounded compile still exits 1 on other
errors (3.93 seconds; sampled peak RSS 726,880 KiB). Artifact:
`build/validation/elisascript-ddbc803d-int-cstr-build.log` and watchdog JSON.
Runtime integer formatting is still unverified on the selected compiler.

Character formatting preserves `cstr` through its three helpers after the
existing one-byte-plus-NUL interned copy. Both character wrapper return
mismatches clear; the changed prelude has no diagnostics. The bounded build
still exits 1 on other errors (5.23 seconds; sampled peak RSS 724,688 KiB).
Artifact: `build/validation/elisascript-ddbc803d-char-cstr-build.log` and JSON.
Runtime character formatting remains unverified.

The view-to-string copy helper now preserves `cstr` through empty, short and
long NUL-producing copy paths; its three callers no longer cast the result
back to `u8&`. This boundary matches the actual terminated output. The bounded
compile reports no diagnostics on the changed helper/callers, but the runtime
entrypoint still has the same 31 diagnostics: this change establishes no
reduction in the remaining compiler failures. The build exits 1 normally in
11.53 seconds, sampled peak RSS 725,872 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-view-copy-build.log` and watchdog JSON.
Copy behavior remains unexecuted on the selected compiler.

Unsigned and floating-point scratch formatting now return `cstr` at their
NUL-producing boundaries. Unsigned formatting checks the writing `snprintf`
length; the float marker scan yields its boolean result with initial false,
retaining the existing formatting and `.0` suffix algorithm. In the bounded
build, runtime entrypoint diagnostics fall from 31 to 5. Remaining diagnostics
include three concat/slice return mismatches and two newly reached global
`perm_arena` borrow conflicts; they require explicit ownership review.
The after build exits 1 normally in 7.86 seconds, sampled peak RSS 726,960 KiB.
Artifact: engine `build/validation/elisascript-ddbc803d-numeric-cstr-fixed-build.log`
and watchdog JSON. The preceding `numeric-cstr-build` attempt compiled unchanged
source after an edit assertion failed; it is not evidence for this repair.
Numeric formatting execution remains unverified.

The permanent integer/character wrappers now use upstream's call-local
`trusted Unsafe.Alias` exception. Review found sequential arena descriptor
access: integer small-string/length-cache allocation goes through alloc_perm,
and character formatting ignores its arena argument before its permanent
copy. Returned strings refer to allocated storage, not the lent descriptor.
Comments identify these paths; exclusivity checks remain active elsewhere.
Both global-borrow diagnostics clear; runtime entrypoint diagnostics fall
from 5 to 3. The bounded compile still exits 1 on other errors (7.59 seconds;
sampled peak RSS 728,672 KiB). Engine artifact:
`build/validation/elisascript-ddbc803d-arena-alias-build.log` and watchdog JSON.
This establishes neither concurrent allocator safety nor an executed launcher.

Concat, scratch concat and string slice now accept optional `cstr` inputs and
return `cstr`, preserving that guarantee in unchanged-input return paths.
Copied paths convert only after writing the terminator or interning a copy;
size guards and slice clamping are retained. The bounded build clears all
three remaining runtime entrypoint diagnostics, with no diagnostics on these
functions. Other string-view carrier and source compatibility errors remain.
It exits 1 normally in 7.23 seconds, sampled peak RSS 726,944 KiB. Artifact:
engine `build/validation/elisascript-ddbc803d-concat-slice-build.log` and JSON.
These operations have not executed on the selected compiler.

## Full compatibility matrix — prover 95db5c6b

The Python 3.14 full matrix completed with status 1 and 63 failed steps,
compared with the preceding run's 72. This is a failing qualification result;
shared/native gates remain open. The new entry-count and pop source controls,
signed call snapshots, qualified call-summary controls and kernel audit pass.
Retained full log: `build/validation/proof-95db5c6b-ddbc803d-full-matrix.log`;
its `-summary.json` records every KEEP_GOING failure and preceding context.

One census failure checked the repository's old compiler pin despite the
explicit build revision override. The census now honors ELISA_COMPILER_REV
while retaining source-tree, binary SHA, Stage1 and frontend provenance checks.
The existing focused census suite passes, including matching and mismatched
override controls. The actual 95db5c6b binary manifest passes the repaired
identity check with ddbc803d; the full census comparison remains to be rerun.
This harness repair does not establish the remaining matrix steps as passing.

The C-string view, view-slice and byte-array view constructors now use the
current upstream runtime's local carrier grants and extent checks. C-string
inputs retain `cstr`; invalid signed extents and malformed byte-array storage
return empty views before pointer arithmetic/indexing. The bounded build
clears all four runtime-string-fragment diagnostics, including three internal
carrier errors, while other launcher errors remain. It exits 1 normally in
8.59 seconds, sampled peak RSS 727,056 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-view-carrier-build.log` and JSON.
View behavior/lifetimes have not been runtime-qualified on this compiler.

The IR effect identity hash helper now has an IR-specific name, preventing
resolution to Ast's private same-named helper, and binds its cross-product
sum as one immutable expression. This preserves the limb formula: each masked
summand is at most 2^32−1, so their sum fits u64. All five ir_model diagnostics
clear in the bounded build; other launcher errors remain. It exits 1 normally
in 9.38 seconds, sampled peak RSS 530,128 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-ir-hash-build.log` and watchdog JSON.
Runtime effect identity behavior remains unverified on this compiler.

Allocator return paths now retain mutable pointer qualifiers through fixed
buffer allocation, arena allocation/reallocation and arena formatting. The
free-list split reference is initialized from its computed in-block address
with explicit pointer effects, replacing an invalid zeroed non-null reference.
The bounded compile clears all eight arena/heap diagnostics and reports none
in either fragment; unrelated launcher errors remain. It exits 1 normally
in 11.50 seconds, sampled peak RSS 635,776 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-allocator-refs-build.log` and JSON.
Allocator behavior has not executed on the selected compiler.

Concurrency allocation now preserves malloc's mutable pointer through return;
AtomicCell constructs its generic slot from the supplied value rather than
zeroing a potentially non-null type. Both associated diagnostics clear in
the bounded compile, leaving nine concurrency diagnostics. The generic worker
result seed still needs completion-protocol review; it was not replaced by
unchecked uninitialized typed storage. The build exits 1 normally in
20.92 seconds, sampled peak RSS 617,600 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-concurrency-init-build.log` and JSON.
Runtime concurrency behavior remains unverified on the selected compiler.

Platform concurrency wrappers now explicitly grant pointer conversion where
Win32 opaque handles are recovered for lock/condition operations and where
pool worker state/record pointers are recovered from submitted handles.
Comments identify Win32 initialization and the nonzero record uintptr
round-trip as the source of those handles. Eight conversion diagnostics clear,
leaving only the generic worker-result seed error in the concurrency fragment.
The bounded compile exits 1 on remaining errors in 5.25 seconds, sampled peak
RSS 658,880 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-concurrency-casts-build.log` and JSON.
No Windows execution or pool synchronization behavior is established.

Generic worker results now use untyped allocated storage instead of an invalid
zeroed R. The worker writes R before release-storing completed=1; result take
requires an acquire load observing completion before its typed read. Existing
join/pool-wait and reference-count release paths remain. Current compiler
codegen_atomic.elisa recognizes both atomic[T] and AtomicSlot[T], supporting
the ordered publication calls. Conversions are confined to two documented
result-storage helpers. The bounded compile clears the last concurrency
fragment diagnostic and reports none in that fragment; other launcher errors
remain. It exits 1 normally in 9.65 seconds, sampled peak RSS 587,088 KiB.
Engine artifact: `build/validation/elisascript-ddbc803d-worker-result-build.log`
and JSON. Native worker execution and adversarial completion controls remain
required before treating this synchronization path as qualified.

HashContext's four updated fields now explicitly permit mutation through the
existing borrowed advance_hash API. Validation receives a complete value
snapshot, preserving the public value validator and checking before updates.
The limb cross-product sum is bound in one expression; its masked summands
fit u64. All eight hash_model diagnostics clear in the bounded compile;
other launcher errors remain. It exits 1 normally in 11.30 seconds, sampled
peak RSS 463,664 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-hash-context-build.log` and JSON.
Hash state transitions and refusal behavior remain unexecuted on this compiler.

LowerState now holds its 21 read-only metadata tables as borrows matching
lower_function's parameters. Its four annotation traversals use bounded
indices; required effects/errors are constructed as owned loop results before
state construction, retaining zero-iteration values, order, empty filtering
and error-name deduplication. This applies Stage1 container value-threading
at the actual allocation-owner boundary. The initial borrow-only attempt
reduced 20 lowering diagnostics to six; the completed change clears all
lower_ast diagnostics. The bounded build still exits 1 on other errors in
7.18 seconds, sampled peak RSS 463,616 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-lowering-owned-build.log` and JSON.
Lowered-program behavior and metadata lifetimes remain runtime-unverified.

Vendored parser and symbol hashing now bind the limb cross-product sum in one
expression, matching the IR hash repair and preserving modulo multiplication.
Both immutable-assignment diagnostics clear; no parser_tokens or symbols
fragment diagnostics remain in the bounded compile. The build still exits 1
on other launcher errors in 8.33 seconds, sampled peak RSS 539,312 KiB.
Engine artifact: `build/validation/elisascript-ddbc803d-vendor-hash-build.log`
and watchdog JSON. Parsed effect/symbol identity behavior remains unexecuted.

Nine bare mutations of legacy lmut SymbolTable values now visibly reassign
the returned table: six recursive duplicate-pattern checks and three private
visibility metadata index calls. No checks or diagnostics were removed.
The bounded compile clears all nine errors in the two semantic fragments;
other launcher errors remain. It exits 1 normally in 7.96 seconds, sampled
peak RSS 687,296 KiB. Engine artifact:
`build/validation/elisascript-ddbc803d-semantic-thread-build.log` and JSON.
Duplicate-pattern rejection and visibility behavior remain runtime-unverified.
