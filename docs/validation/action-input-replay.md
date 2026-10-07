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
