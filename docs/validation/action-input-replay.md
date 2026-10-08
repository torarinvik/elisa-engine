# Action-input replay

Validated on 2026-09-30 with the pinned Stage1 compiler. This is I01 progress
toward "replayed input behaves consistently".

`src/runtime/action_input_replay.elisa` records tick-stamped entries for
input events, context switches and device connect/disconnect. Games call
`feed_event`, `feed_context` and `feed_connection`. Each call records the
entry and applies the same entry to the live `ActionInput::Input`. Analog
values are quantized to the Application queue's 20 fractional bits before
both uses, so live and replayed values match exactly.

`replay_tick` applies one tick's entries after `ActionInput::begin_frame`.
The log holds 1024 entries. An entry with a backwards tick, or one past
capacity, still reaches live input but is not recorded. It is counted in
`dropped`, and `replay_trusted` then reports false.

## Test

`test/action_input_replay.elisa` is in the gate's unit-test list. It runs a
12-tick live session:

- jump is pressed, held, released and pressed again;
- the gamepad stick is pushed below and then above its 0.2 dead zone;
- the gamepad is unplugged mid-push and reconnected;
- the UI context opens, Esc fires its menu action, and gameplay resumes.

For each tick, the test takes a digest of the held, pressed and released flags
and the quantized value of every action. Replaying the log into a freshly
bound `Input` gives the same digest on all 12 ticks. The test also checks the
backwards-tick refusal, the capacity count (1023 kept and 78 dropped) and
quantize clamping.

Negative control: having the replay skip Disconnect entries makes the test
fail with code 3. A first control mutated `apply_entry`, which live and replay
share, so it could not fail; the recorded control mutates the replay side
only.

## Byte form

`src/runtime/action_input_replay_codec.elisa` writes the log as "EIR1", a
u32 count and 16 bytes per entry, up to 16,392 bytes in total. The test now
replays from the decoded bytes. Decoding fills the log only when every entry
is valid, and each of these damaged inputs is refused with an empty log:

- a truncated length (BadLength);
- an unknown kind byte (BadField);
- a tick that moves backwards (NonMonotonicTick);
- a wrong magic (BadMagic).

Negative control: decoding the pressed flag from the released bit makes the
replay diverge with code 3.

## Recording Application events

`src/runtime/action_input_recording.elisa` adds `record_application_event`,
which routes events the same way as
`ActionInputRuntime::apply_application_event`, and `begin_frame_recorded`,
which drains the Application queue. A new `ClearDevice` entry kind (byte 4)
records the held-state clears from focus loss, overflow and the clear before
a gamepad disconnect.

`test/action_input_recording.elisa` is in the gate list. It feeds the same
14 ticks of Application events, in two input maps, through the plain runtime
and through the recorder:

- keyboard, mouse and stick presses;
- focus loss while the stick is held;
- a queue overflow;
- a gamepad unplugged and reconnected.

The two maps agree on every tick, and replaying the recording from bytes
reproduces all 14 digests. The event values are exact at 20 bits, as the
Application queue produces them.

Negative control: skipping the gamepad clear on focus loss in the recorder
makes the test fail with code 1.

## Gaps

- The log is not merged into `Replay::Recorder`.
- No packaged game records a session yet, and recordings are not written to
  disk.

## Current post-audio triage

With prover commit `65b58b71`, both ActionInput reports remain 199/228, 33 findings, zero semantic errors. Four functions refuse at the 128-fact snapshot bound: refresh_action, bind_checked and rebind_checked observe 129 facts; bind observes 131. The next admission fix must reduce or select relevant state facts rather than silently raise the bound. An ignored typed-libm-result probe removes two expression refusals in apply but replaces them with index-bounds-opaque refusals, leaving the same result; it was not applied to production. Reports: `build/validation/action_input_{context,deadzone}-current.json`, `build/validation/action-input-typed-math.json`.

### Operator marker snapshot overhead minimized

At refresh_action's 129-fact snapshot, 70 entries are operator markers (42 builtin and 28 untrusted), compared with 15 primitive scalar markers and 10 arithmetic facts. Each marker records one protocol on one typed place; a compact representation must preserve all protocol and overloaded-operator distinctions. Prover `examples/operator_witness_snapshot_budget.elisa` reproduces the 128-fact limit with six fixed bool arrays and nine unused f32 declarations: 133 facts, one budget finding, no semantic errors. The float locals intentionally have literal initializers, so no float arithmetic law contributes to the refusal. Report: `build/validation/operator-witness-snapshot-budget.json`. Next task is compacting protocol witnesses with matching independent replay, under the unchanged snapshot and traversal limits.

### Compact complete protocol witnesses qualified

Generation `7e138bc0c2674c559c9a56e89787d74a` records complete seven-protocol sets with All or Index.All tags in the existing marker nodes. Mixed source-override cases retain separate protocol markers; builtin enum equality exclusions retain their prior separate shape. Queries expand only the seven named protocols. Independent replay continues its existing conservative untrusted-marker decoding; no numeric, IEEE or resource law was added. Snapshot/scalar/traversal limits are unchanged.

The snapshot reproducer now proves/replays all bounded writes; wide/missing guards and shared writes remain refused in both proof routes. Float resources (11), local records (16), fixed index (7), enum resource (5), local decimal (11), numeric-cast refusal, operator/global stability and IEEE forgery controls pass. Indexed-boolean predicate remains unqualified at 19/22 replay: the saved pre-change product fc31a8ca has the same three gaps.

ActionInput advances past every snapshot-budget refusal, checking 242 obligations with 201 replayed, zero gaps and 41 findings. The larger inventory includes rows previously skipped by budget refusal; this is not proof completion. Full uncached engine coverage stays 71/73, only the two ActionInput reports fail. Reports/logs: `build/validation/action-input-protocol-groups.json`, `build/validation/proof-protocol-group-engine-sweep.log`, `build/validation/proof-protocol-group-build-2.log`. Next triage: builtin float call-result operator admission and the newly reached indexed binding/state writes.

## Float call result admission — 2026-10-07

Paired generation `13db254a25114c8a89925719b2e66627` uses the same exact
compiler `8006b660` snapshot. Build: `build/validation/proof-float-call-shape-build-2.log`.
The runtime protocol gate now reads declared source function return types and validated,
unique extern signature return metadata for `f32`/`f64` in float mode. Extern admission
requires matching nonvariadic arity, valid parameter/return metadata, a nonreference
nonoptional result, and no source function of the same callable name. Source protocol
overrides still refuse admission. This changes receiver classification only; it adds
no IEEE arithmetic identities, purity or repeated-call congruence.

`test_float_call_result_shape.py` passes six cases through both whole-file and function
routes: extern f32/f64 and source f32 accepted; NaN-result reflexivity, overridden
equality and record-valued results refused. Existing float resource, literal forgery
and compact snapshot controls pass. The test is included in the prover matrix.

Actual context report: `build/validation/action-input-float-call-shape-2.json`,
244 obligations, 205 proven/replayed, 39 findings, zero replay gaps or semantic errors.
Previously 242/201 with 41 findings; previously stopped expressions now contribute
additional obligations. `approx_eq` and `refresh_action` unsupported-expression rows
are gone. Deadzone report: `build/validation/action-input-deadzone-float-call-shape.json`.
Both engine proofs still fail. Next inspect the remaining `bind_checked` expression
and bounds on copied binding/state slots. Full matrix qualification remains open.

## Ordinary for-loop outer bounds — 2026-10-07

Generation `15ce730f72eb4e62bf03683ff83af0e6` fixes the no-invariant ordinary
for-loop exit rule: a body containing a local declaration previously triggered whole
frame forgetting. Exit now uses the existing targeted arbitrary-iteration entry rule,
resymbolizing written/aliased roots and purging dependent facts. The existing mutated
reference-parameter cleanup remains. Parallel loops and checked-invariant exit rules
are unchanged. No iteration body fact is exported as a post-loop assertion.

`examples/loop_readonly_outer_index.elisa` reproduces a guarded outer index, copied
record, read-only search loop and two later writes. Baseline: 11/13 with two upper-bound
findings; repaired: 13/13 with complete replay. `test_loop_readonly_outer_index.py`
passes five cases on both routes: read-only accepted; wide/missing guard, index rewrite
and borrowed index mutation refused. It is included in the prover matrix. Captured
loop entry, deterministic operator/global invalidation, local-digit decimal and
for-saturation controls pass. `test_loop_state_joins.py` still fails on exactly the
previous 45/47, two replay-gap baseline; this slice does not qualify that suite.

Actual ActionInput context: `build/validation/action-input-for-outer-index.json`,
256 obligations, 223 proven/replayed, 33 findings, zero replay gaps and semantic
errors. All rebind_checked findings are removed; additional checked downstream code
contributes 12 obligations. Build log: `build/validation/proof-for-outer-index-build.log`.
Full uncached engine sweep remains 71/73 with only the two ActionInput failures
(`build/validation/proof-for-outer-index-engine-sweep.log`). Next fix the captured
search-loop assignment in bind_checked and indexed apply bounds.

## Captured search-loop assignment minima — 2026-10-07

Prover commit `90160163` stores three actual source minima rather than changing
engine loop syntax. `captured_search_assignment.elisa` (8 lines) rejects assignment
of a loop value before checking its body: 3/4, one expression-unsupported finding.
`captured_search_declaration.elisa` admits declaration of the same search: 4/5, one
upper-bound finding on the post-loop write after excluding the sentinel.
`captured_search_invariant.elisa` explicitly states `slot <= 4`: all 8 producer
obligations certify, but only 7 replay, exposing a third source-binding gap.
All three have zero semantic errors. Reports are under
`build/validation/input-minima/search-{assignment,declaration,invariant}.elisa.json`;
the assignment's original report is `captured-search-assignment.json`.

A diagnostic-only variant using `return if slot >= 4` instead of sentinel equality
passes 5/5 and replays. This is evidence that the remaining declaration failure is
the missing bounded search result, not generic indexed-write admission. No engine
guard or loop expression was changed. The required repair must execute block
assignment bodies before binding their yield, preserve inner accumulator shadowing,
and authenticate invariant/result facts independently in replay. Existing block
declaration flattening rejects visible-name shadowing and is insufficient here.

## Search-break invariant replay — 2026-10-07

Generation `66108e65f1aa42439766e1540984b446` repairs the invariant minimum:
`captured_search_invariant.elisa` now proves and independently replays 8/8, zero
findings, gaps or semantic errors. Report:
`build/validation/captured-search-invariant-repaired-6.json`; final build log:
`build/validation/proof-search-break-build-6.log`. Initial drafts exposed a compiler
backend decline on a literal constructor pattern and mismatches with parser wrapper
and header type representation; these drafts were not published as qualified fixes.

The new source replay rule matches the original value block, its single accumulator,
loop binder, invariant and exact lowered assignment immediately followed by an
unlabelled break. It requires the equality's assignment position and range binder
identity to match, and allows that equality only at this break's original invariant
obligation. It peels only empty capture-free outer wrappers. Header accumulators use
the parser's bare usize type. No initial equality is retained at preservation and no
new post-loop assertion is introduced. Calls, moves and source operator overrides in
guards/iterables are refused by this rule.

`test_search_break_replay.py` passes six cases on both routes: valid accepted; wide
range, invalid initializer, false invariant, wrong break value and false exit
postcondition refused. Captured-entry, local-digit and decimal-saturation controls
pass. The suite is wired into the full prover matrix. Full uncached engine sweep
remains 71/73, only the two ActionInput reports failing
(`build/validation/proof-search-break-engine-sweep.log`). Block assignment and
implicit bounded search result admission remain required; engine behavior is unchanged.

## Scoped block assignment execution — 2026-10-07

Prover `8fb5b812`, paired generation `70a24da921204155bd18681ae8289ef4`,
checks an existing plain local's `<-` value-block assignment before the expression
fallback. It executes every body statement and the yielded expression in a private
symbolic scope, with copied fixed-place state, normal frame-write checking and the
existing analysis budgets. A header accumulator may shadow the outer target.
Nested safety, call and contract obligations are checked rather than skipped.

At exit the handler exports flow validity/transfers and conservatively invalidates
the assigned outer target and every root reachable through the block's writes,
borrows or calls. Local facts and accumulator entry values are not exported. Result
precision remains intentionally unavailable until original-source result evidence
is implemented; this is an admission/coverage repair, not bounded-result completion.
Field/index assignment targets and compound assignments retain prior handling.

`test_value_block_assignment.py` passes eight cases on both routes: guarded shadowed
and distinct accumulators and zero iterations accepted; wide iteration range, wide
post-guard, sentinel-only guard, other outer mutation and false stale-result
postcondition refused. Search-break replay, read-only outer index and captured-entry
controls pass. The suite is wired into the prover matrix.

Actual ActionInput report `build/validation/action-input-block-assignment-2.json`:
257 obligations, 225 proven/replayed, 32 findings, zero gaps or semantic errors.
The captured assignment unsupported row is gone and its loop body contributes
checked safety obligations. All four remaining bind_checked findings are upper
bounds on the search result. Full uncached engine sweep remains 71/73 with only
the two ActionInput failures (`build/validation/proof-value-block-assignment-engine-sweep.log`).
Build log: `build/validation/proof-value-block-assignment-build-2.log`.

## Qualified search constants and mixed record mode — 2026-10-07

Prover `487346df`, generation `081ffdc430de423b883cd17217f70322`, normalizes
qualified integer constants in local source equations using the existing exact
source-path, declaration/value and shadowing validator. Both global-constant trace
kinds can carry a qualified reference; the kind alone grants no constant value.
Search replay normalizes the initializer, invariant and iterable before matching.
`captured_search_qualified_constant.elisa` now proves/replays 8/8 with no errors/gaps
(`build/validation/qualified-search-repaired.json`). Ten search cases pass on both
routes, including another module's same-leaf constant as an accepted control and a
wrong-owner initializer as a refusal. Captured-entry and local-digit controls pass.
Build: `build/validation/proof-qualified-search-build-3.log`. Engine sweep remains
71/73, only the two ActionInput failures
(`build/validation/proof-qualified-search-engine-sweep.log`).

A diagnostic-only state_slot contract/invariant probe now replays all seven
certificates but still has two producer failures (preservation and the postcondition),
report `build/validation/state-slot-contract-qualified-2.json`. No engine source
contract was committed from this incomplete probe. Adding 4/8/12/16 unrelated integer
array fields to the passing search minimum does not reproduce these failures. Adding
one unused f32 field does: `examples/captured_search_float_record.elisa` is 6/8,
zero replay gaps/errors, two producer failures. Its otherwise identical integer-only
record is 8/8. An unrelated float function also does not reproduce the failure.

Next repair integer goal admission when a parameter record contains floats, preserving
all IEEE false-law refusals. Then verify the state_slot bounded contract and carry
source-authenticated search bounds through block assignment and branch joins.

## Integer loop binders in mixed records — 2026-10-07

Generation `5f8c12fefafb4cfea21de65368e874a3` fixes the float-mode integer filter.
Counting-loop binders are internal tagged tuple atoms; the filter previously dropped
their integer premises even when an exact signed width witness existed. Tuple syntax
now admits integer ordering only with an exact unsigned-place or signed type witness.
No float type/receiver witness grants integer arithmetic. Qualified contract prefixes
in the search-break replay rule use the same validated normalization as its other
source expressions. Build: `build/validation/proof-mixed-search-build-2.log`.

`test_mixed_record_search.py` passes seven cases on both routes: f32/f64 records
accepted; wide range, invalid initializer/break, NaN reflexivity and NaN ordering
refused. All IEEE literal forgery controls pass. Eleven search-break cases, captured
entry, read-only outer index, block assignment, float ownership and deterministic
operator/global controls pass. New suite is included in the prover matrix.

The actual state_slot contract probe now proves/replays 9/9, no errors or gaps
(`build/validation/state-slot-contract-mixed-repaired-2.json`). Engine state_slot
therefore now declares `ensure result <= Limits::MAX_ACTIONS` with the matching
loop invariant, retaining its loop expression and zero-iteration sentinel.
All 215 runtime tests pass uncached in 28s, forced unavailable audio device
(`build/validation/action-input-state-slot-runtime.log`).

Actual context report: `build/validation/action-input-mixed-search.json`,
261 obligations, 229 proven/replayed, 32 findings, zero replay gaps or errors.
Full uncached engine sweep remains 71/73
(`build/validation/proof-mixed-search-engine-sweep.log`). Four bind_checked upper
bounds remain because block assignment still discards its accumulator result
precision. Next carry checked, source-authenticated accumulator bounds through the
assignment scope and branch join. Full prover matrix/shared/native qualification
remains open.

## Checked identity accumulator result bounds — 2026-10-07

Generation `495680a60ab645d585777823bf6a1789` preserves a checked upper-bound
invariant at a block assignment when the block yields the same usize accumulator
name as its existing outer target. The exact two-statement value-block shape is
required (accumulator declaration, for loop); only nonnegative literal upper-bound
facts present at normal private-scope exit are eligible. The fact must have an
original loop-invariant trace for that loop's source line and owner. The original
fact and trace are reused; initial equalities, arbitrary body facts and renamed
results do not escape. Invalid entry/preservation removes the invariant at loop
exit and therefore grants no result bound.

Replay now locates the lowered search assignment within bounded original-source
statement trees, including nested if branches. Owner identity, assignment position,
loop binder identity, immediate unlabelled break and invariant goal remain exact.
Primitive operator overrides in the original invariant refuse this route. The walk
requires one unique match and retains depth/work limits. This removes the earlier
contract-only owner-prefix restriction for the source-positioned break binding.

`test_block_result_bounds.py` passes nine cases through both routes: identity, nested
assignment and zero iterations accepted; invalid entry/preservation, loose invariant,
wide range, later rebind and different result name refused. Block assignment, mixed
record arithmetic, search replay and IEEE literal-forgery controls pass. New suite
is wired into the full prover matrix. Build:
`build/validation/proof-block-invariant-result-build-2.log`.

Engine bind_checked now states the checked search invariant. Runtime: all 215 tests
pass uncached in 32s (`build/validation/action-input-search-result-runtime.log`).
Actual context report `build/validation/action-input-block-result-current.json`:
264 obligations, 232 proven/replayed, 32 findings, zero gaps or errors. Full engine
sweep remains 71/73 (`build/validation/proof-block-invariant-result-engine-sweep.log`).
The actual probe retains slot <= MAX_ACTIONS immediately after assignment; later
mutable input writes drop it and the copied binding-count bound. Four bind_checked
upper findings remain. Next repair stable scalar snapshot facts across aggregate
mutation; do not count assignment-exit retention as completed bind_checked coverage.

## Scalar copy bound diagnosis — 2026-10-07

`../elisa-engine-proof/scripts/diagnose_scalar_field_snapshot.py` records six
minimal cases through whole-source and function JSON routes. The safe field copy
has 4/5 obligations proven/replayed; explicit local guard and parameter controls
have 5/5. Stale current-field indexing and an insufficient entry guard each leave
two obligations open; rebinding the mutable copy to the extent leaves one open.
All twelve reports have zero replay gaps, semantic errors or trusted assumptions.
Log: `build/validation/scalar-field-snapshot-controls.log`. This is a diagnostic
for an open defect, not a passing acceptance gate or engine coverage increase.

Producer mutation cleanup already retains facts stated directly over the local.
The missing step is capturing a receiver-free consequence at declaration time.
Existing numeric snapshot transfer uses proof-step premises, whose replay occurs
at the later consumer line. A mutable field-copy equality cannot be made globally
live there: the source field may have changed. The fix needs authenticated
pre-state premises at the declaration, with an independently checked immutable
local lifetime at consumption. Preserve refusal of stale field indexing, mutable
local rebind and insufficient entry guards. Do not add redundant engine guards
or grant persistent equality to the current record field.

## Full matrix qualification failed — 2026-10-07

The keep-going full matrix ran against generation
`495680a60ab645d585777823bf6a1789`; log:
`build/validation/proof-current-full-matrix.log`. Owned session `39578`
finished with exit status 1 and `73 step(s) failed (KEEP_GOING)`. Beyond the earlier loop-state gaps,
collection-frame and loop-exit-frame fixtures report replay gaps. Portable package
identity validation also refuses these prebuilt products because their recorded
prover source is dirty at `eb5c14b1`, although the source changes have since been
committed. Source-binding harness failures need separate exact-source diagnosis.

A rebuild of both products from clean committed prover `dfa1960a` with the
immutable compiler 8006 snapshot has started (log
`build/validation/proof-clean-dfa1960a-build.log`, session `91345`); its terminal
result and fresh identities remain to be checked. Then rerun failed cases plus
the complete matrix. A clean-source rebuild addresses
provenance; it does not establish that replay or harness failures are repaired.
The engine's 71/73 proof result remains partial and does not qualify the matrix.

A direct check of `rejected_loop_counter_invariant.elisa` distinguishes a matrix
message from its cause: all five invalid functions remain body-unverified with
the expected seven findings. The report has 15 obligations, seven replayed and
one replay gap among eight certificates. The matrix's "fresh rebind symbol"
assertion fails on replay accounting; this probe does not demonstrate acceptance
of those false claims. Report: `build/validation/rejected-loop-counter-current.json`.

## Clean paired provenance confirmed — 2026-10-07

The build in session `91345` finished with status 0 and published generation
`5333c30442174b81b48977ad3c84ab60`. Both compile inputs were unchanged, so the
build reused the products and refreshed their immutable paired provenance.
Manifest sidecar and binary SHA-256 checks passed. Both products record clean
prover head `dfa1960ad80916ea294300e35d66dce2d7612c0d`, source-tree hash
`ad8fed56051126d78d193ca0ca69af694bfd5933f1b96819339e65d9dc7747ae`.
Proof binary SHA-256:
`a6da6975ee256272a7b59f3bdabaea7642f16c7d9b26529f22bbba76375ed7ff`;
replay binary SHA-256:
`7cf149ef651cc605d7f80d61bad862197387b8008b5e21326c199a1e86e71580`.

`test_portable_package_json_scan_boundaries.py` now passes its provenance and
cap-1/cap/cap+1 checks with positive replay and bounded fresh-process refusals
(`build/validation/proof-clean-package-scan.log`). `test_loop_state_joins.py`
and `test_captured_loop_constants.py` still fail on this clean pair
(`proof-clean-loop-state.log`, `proof-clean-captured-loop.log` in the same directory).
This closes dirty-source provenance for the pair; it does not repair the matrix
or establish whole-engine qualification. Next repair source-authenticated scalar
snapshot and loop binding replay, then repeat the full matrix with clean products.

## For identity preservation replay — 2026-10-07

Prover `9cd24ca7`, clean paired generation `9d50cfbe7ee945d4bf23ee70b1f215c9`,
adds a source route for the first identity update in a for-loop preservation
certificate. Replay matches the source owner, unsigned local declaration, exact
assignment position, `count <- count` body, original invariant and the invariant
with the current scalar replaced by the exact first rebind symbol. The certificate
must contain that original invariant; entry and later-use goals cannot borrow
this equation. Source primitive overrides and unsupported declaration environments
refuse; wrapper walks retain depth limits. Direct captured locals, returned loop
values and bound loop results use the same source check.

`test_for_identity_update.py` passes seven cases through both JSON routes:
three value forms accepted; wrong entry, growing update, wrong update and false
invariant refused. It is included in the full matrix. The existing captured-loop
constants suite now passes, including shadowed/sibling constants, duplicate capture
and depth-budget controls. Logs: `proof-for-identity-controls.log` and
`proof-for-identity-captured.log` under `build/validation/`.

The loop-state fixture improves from 45/47 to 46/47 proven/replayed; only
`alias_rebind` retains a replay gap (`proof-for-identity-loop-state.json`). Search
break and checked block-result controls pass. The full uncached engine proof sweep
remains 71/73 with only the two ActionInput reports failing
(`proof-for-identity-engine-sweep.log`). Scalar field snapshots, alias rebind and
other full-matrix failures remain open. Clean build provenance is recorded in
`proof-for-identity-clean-build.log`; full matrix qualification is still required.

## Scalar copy/restore for preservation — 2026-10-07

Prover `e5ea68af`, clean paired generation `45671c341a4145f2a0a2487735af8b54`,
authenticates the exact three-statement for body: invariant, immutable unsigned
scalar copy of the accumulator, and plain assignment restoring that copy. Both
binding equations are admitted only at this loop's original preservation goal.
Owner, unsigned accumulator declaration, source positions of the copy/assignment,
source invariant and original range binder/offset are checked. The range witness
separates preservation from entry certificates sharing a line and goal. Calls,
extra body writes, mutable copies, source primitive overrides and unsupported
prefixes/environments do not receive this route; source wrapper walks are bounded.

The full `test_loop_state_joins.py` now passes: 47/47 obligations proven/replayed,
including alias_rebind, with its existing adversarial fixture still refused.
`test_for_alias_update.py` passes six cases through both JSON routes, all with
zero gaps: copy/restore accepted; missing entry bound, different copy, changed
update, mutable changed copy and stale later postcondition refused. The new suite
is included in the full matrix. Identity-update and captured-constant suites pass.
Logs under `build/validation/`: `proof-for-alias-controls-clean.log`,
`proof-for-alias-loop-state-clean.log`, `proof-for-alias-identity-controls.log`,
`proof-for-alias-captured-controls.log`, and `proof-for-alias-clean-build.log`.

The uncached engine sweep remains 71/73, only the two ActionInput reports failing
(`proof-for-alias-engine-sweep.log`). This repairs one existing full-matrix fixture;
other matrix failures and scalar field snapshots across actual record mutation
remain open. Full matrix qualification still needs a fresh complete run after
those fixes. No runtime source behavior changed in this slice.

## Unsigned field-copy experiment and index trust shortcut — 2026-10-07

A temporary producer call to `proof_transfer_numeric_snapshot_facts` for unsigned
field initializers makes the minimal copy case 5/5 and a variant with no earlier
index access 3/3. Stale current-field indexing, insufficient entry guard and
mutable copy rebind remain refused. The actual ActionInput context remains
232/264 with the same four bind_checked upper findings and zero replay gaps.
Reports: `build/validation/field-copy-experiment-minimal.json`,
`field-copy-no-prior-index.json`, and `field-copy-experiment-action-input.json`.

The apparent minimal replay success does not validate the copied bound's
pre-state premises. In `src/proof/check/index_checks.elisa`, the fixed-array route
runs `proof_goal(index < literal_extent)` without recording its derivation and
then imports `index < object.count` as a type-bound fact. The final certificate
contains that upper goal itself under type-bound provenance. Replay can use this
fact without the copied-bound proof step. A true array type fact is
`object.count == literal_extent`; a particular index's bound needs its checked
premises. Authenticate that conversion before claiming snapshot replay success.
The signed-count conversion route and IndexN projection need the same review.

The experiment was removed after its build finished. The original clean prover
source was restored and paired generation `513c2f1efd2a4c118c0f00793529d9ca` was
published successfully (`proof-field-copy-restore-build.log`). The original six
snapshot diagnostic cases again match their baseline counts on both JSON routes
(`field-copy-restored-controls.log`). No producer experiment is committed or
left enabled. Next replace the index-specific type fact with a source-checked
fixed-count fact and ordinary certificate derivation, then implement copied-bound
pre-state replay with the uncovered premises visible.

## Fixed-array count projection repaired — 2026-10-07

Prover `95abf650`, clean pair `524c32109d4f4e3b9a2b3725c3ede70b`, replaces the
single-index and slice index-specific type facts with the genuine type property
`object.count == literal_extent`. The count also receives its usize/scalar type
witness independently of aggregate scalar-walk budget. The ordinary upper-bound
certificate must now replay the index or endpoint premises. Obligation counts
remain unchanged. IndexN already certifies the literal dimension bound directly;
the dynamic signed-count conversion route remains a separate review task.

`test_fixed_count_projection.py` passes seven positive/negative fixtures, including
nested record fields (26/26), literal indices and slices. Certificates must contain
a count equality and cannot contain their own upper goal as a type-bound premise.
Loop-state, checked block-result and search controls pass. The new suite is wired
into the full matrix. Logs: `proof-fixed-count-controls.log`,
`proof-fixed-count-loop-state.log`, `proof-fixed-count-block-controls.log`,
`proof-fixed-count-search-controls.log`, `proof-fixed-count-clean-build.log` under
`build/validation/`.

The change exposes unsupported source-copy equations previously hidden by the
index-specific fact. Actual input context is now 230/264 proven/replayed, 32
findings and two replay gaps (`fixed-count-action-input-current.json`).
AudioAnimEvents is 54/56 with two replay gaps in anim_emit_event and push_event
(`fixed-count-audio-anim-current.json`). Both use an immutable scalar field copy
at the immediately following array write. The minimal scalar-copy case now has
3/5 proven/replayed, one ordinary finding and one replay gap; its passing local
guard/parameter controls and invalid controls remain captured by the updated
explicitly diagnostic script (`proof-fixed-count-snapshot-controls.log`).

The full uncached engine sweep is now 70/73: ActionInput context/deadzone and
AudioAnimEvents fail (`proof-fixed-count-engine-sweep.log`). This is incomplete
qualification, not a claim of a runtime behavior regression or completed snapshot
repair. Next authenticate a scalar field copy at its next pure use before any
source-field mutation; then implement bounds captured at declaration for later
uses across mutation. Do not restore the index-specific type fact to hide gaps.

## Field copy at the next indexed write — 2026-10-07

Prover `2b9ec8d6`, clean pair `0d414014c0fd407288e0d15f3242b7ba`, authenticates
an unsigned field-copy equation while the immediately following pure indexed
assignment evaluates its index. Replay matches the original declaration,
initializer, declaration/consumer positions and exact index goal, within the same
source statement list. The source root must be an unshadowed formal; the local
cannot shadow a formal. Unsigned widths come from the current certificate's type
facts and must fit the declared local width. Calls, moves, local mutation in the
consumer expression and source operator overrides refuse. Region and if scopes
are traversed with depth/work limits. This is equally valid for a fresh mutable
local: there is no intervening rebind before this read. It grants no equation at
later uses after a source-field or local mutation.

Seven cases pass through both JSON routes: immediate, fresh mutable and region
copies accepted; insufficient guard, different field, rebound copy and changed
current-field indexing refused, all with zero gaps. The suite is in the full matrix.
The snapshot diagnostic returns to its original counts (4/5 for the open copy
across mutation; passing local guard/parameter controls). All seven fixed-count
fixtures now pass; the test correctly distinguishes IndexN's directly certified
literal dimensions from `.count` goals. Loop-state controls pass.
Logs under `build/validation/`: `proof-field-next-write-controls-clean.log`,
`proof-field-next-write-snapshot-controls.log`, `proof-field-next-write-count-controls.log`,
`proof-field-next-write-loop-controls.log`, and `proof-field-next-write-clean-build.log`.

AudioAnimEvents is restored to 56/56, zero findings/errors/gaps
(`field-next-write-audio-current.json`). The full uncached sweep is 71/73 again,
only ActionInput context/deadzone failing (`proof-field-next-write-engine-sweep.log`).
Actual input context remains 230/264 with 32 findings and two gaps
(`field-next-write-action-input-current.json`): both gaps involve the scoped
`device_index(event.device)` result used by apply, not a field-copy equation.
Next authenticate those captured call summaries and implement immutable copied
bounds at declaration for later consumption across record mutation. The fixed-index
shortcut remains removed; full matrix/shared/native qualification remains open.

## Scoped call-result aliases — 2026-10-07

Prover `2d0aa811` traverses local declarations inside value-block initializers
when authenticating call-summary aliases. The final expression participates in
the lexical lifetime and mutation checks. Exact binding/call source positions,
argument mapping and dependency certificates retain their existing validation;
block-local aliases do not establish facts at consumers outside the block.

`python3.14 ../elisa-engine-proof/scripts/test_scoped_summary_alias.py` passes
five cases through both JSON routes: direct/nested scoped bindings accepted,
rebound/out-of-range indices and wider callee results refused, all without gaps
or semantic errors. The existing disjunction call-domain suite passes alias,
reassignment, domain, false-claim and malformed-replay controls. The new suite
is wired into the full matrix.

`scoped-summary-action-input.json` records 264 obligations, 232 proven/replayed,
32 findings, zero gaps and zero semantic errors. The uncached sweep remains
71/73 (`proof-scoped-summary-engine-sweep.log`), with both ActionInput reports
still failing on unproven obligations. Immutable copied bounds across mutation
and full matrix/shared/native qualification remain open.

## Guard loss before copying — 2026-10-07

Inspection of context goal 86 shows that `bind_checked` has already lost the
`input.binding_count < MAX_BINDINGS` guard before `binding_slot` is declared.
Goals 88/90/92 then lack the slot range after the binding-table write. Thus the
four findings cannot all be attributed to copied bounds across later mutation.

The minimized `test/repro/read_only_search_field_bound.elisa` in the prover
retains the original field guard, a read-only search with a captured record,
and a subsequent scalar copy/indexed write. Reports under `build/validation/`
show: `read-capture-copy.json` 6/8 with one replay gap; removing the record
capture gives 7/8 with zero gaps (`read-capture-copy-no-capture.json`); removing
the loop gives 3/3 with zero gaps (`read-capture-copy-no-loop.json`). All have
zero semantic errors. Repair loop read/write classification and captured-result
source replay alongside copy-time snapshot authentication; preserve mutation,
alias and shadowing refusals. These are diagnostic results, not acceptance.

### 2026-10-08: copied-bound producer isolated from replay

An isolated worktree (`../elisa-engine-proof-snapshot-repair`, branch
`codex/scalar-copy-snapshot`) adds existing numeric snapshot consequence transfer
after unsigned field binding. Paired experimental generation
`807ab43d535245b0a4b0f31afb8744ac` builds with frozen compiler `52d60fcf`
and exact detached compiler source. Both JSON routes now produce five
certificates for the original copy-across-mutation source, with no findings;
only four replay, leaving one gap on the final index-upper obligation at line 9.
The previously absent fact is `not (slot >= 4)`, derived at copy line 6 with
13 pre-state premises. Replay correctly refuses that derivation without
copy-time source authentication. This is not a completed repair or a passing gate.

Local-guard and parameter controls retain 5/5 with zero gaps. Stale-field,
rebound-copy and wrong-entry cases retain their original refusals and zero gaps,
through both JSON routes. Reports and exact inventory are retained in
`build/validation/scalar-snapshot-transfer-reports/`; log:
`scalar-snapshot-transfer-controls.log`. The producer-only change remains
experimental in that worktree; the production prover and native gate continue
to use the unchanged qualified generation. Next authenticate the derived
consequence at copy time without granting equality to the mutated current field,
and retain independent source/forgery checks before integrating either side.

The isolated repair now reconstructs a copy-time guard independently from source:
unique immutable unsigned local, formal-rooted primitive field with fitting width,
exact dominating early-return comparison, and no destination writes. It admits
the copied inequality only; source-field equality is not made live after mutation.
Unknown preceding statements, operator overrides, ambiguous field types and
primitive-name aliases/types remain unsupported. Producer and replay changes
are committed on `codex/scalar-copy-snapshot` as `879d3726`, not yet integrated
into the engine prover branch.

The initial source-authenticated product passes nine accepted/refused cases
through both JSON routes, including the original 5/5 copied-bound repair,
pre-copy mutation, wrong source field and a fall-through guard. It also passes
the uncached 73-report engine sweep, all 4,246 obligations with zero diagnostics
or replay gaps. Final hardening adds primitive-shadow refusal, literal-width
checks and malformed summary refusal. Its compiled O0 source-gate harness passes
the authentic copy and rejects wrong line/literal/local/kind/summary metadata
and a source alias (`scalar-copy-bound-source-forgery-typed.log`, 30.98s,
1,717,424 KiB peak RSS). These are independently compiled source checks, not
serialized JSON filtering. Both regression suites are added to the full matrix.
The final committed paired build is running (`prover-scalar-copy-final-build.log`)
before repeating focused controls and the uncached engine sweep on that exact
product; full compatibility remains open.
