# Action-input replay follow-ups

Continued evidence from [Action-input replay](action-input-replay.md).

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

Final pre-merge generation `67421177829c42bbb6143932145f7a21` authenticates clean
prover `879d3726` and both binary hashes. It passes the nine-case copied-bound
suite, seven-case immediate-copy suite and updated snapshot diagnostic through
both JSON routes, all with zero gaps and preserved refusals. Its uncached engine
sweep passes all 73 reports / 4,246 obligations, zero semantic diagnostics or gaps
(`scalar-copy-final-engine-sweep.log`, 6.71s, 171,296 KiB peak RSS). Reports
are retained in `scalar-copy-final-engine-reports/`.

Prover main advanced meanwhile through `a98acb6c`. Merge `53b25437` in the
isolated repair branch incorporates its frame/resource/loop/stale-rebind gains,
keeps contiguous obligation-attempt inventory checks while accounting for proven
frame rows, combines both typed-return and source-binding paths, and preserves
current range-binder handling. Fixture theorem types now match the merged model.
The merged clean paired build is running (`prover-scalar-copy-main-merge-build.log`)
before repeating focused and engine qualification. The production engine prover
branch is not yet advanced; full compatibility acceptance remains open.

The first merged build rejects two automatic-merge duplicates in
`immutable_bindings.elisa`: a repeated stable-condition helper and an unreachable
second conditional-initializer arm. Follow-up `cf280488` retains the existing
source-exact conditional-value path (including prior immutable captures) once,
removing the redundant implementation. The clean merged paired build retries
in `prover-scalar-copy-main-merge-fixed-build.log`. The nine-case copied-bound,
seven-case immediate-copy and updated diagnostic suites all passed against
the final pre-merge product; merged-product acceptance is still pending.

Merged clean generation `0984596f9efb4ad0bb6c48d27258f3ad` authenticates `cf280488`
and checked hashes for both products. It passes the nine-case copied-bound suite,
seven immediate-copy cases, range-binder resource refusals and compiled source
forgery harness. Its uncached engine sweep remains 73/73 / 4,246 obligations,
zero diagnostics or replay gaps (5.43s, 165,264 KiB peak RSS). Reports are retained
in `scalar-copy-merged-engine-reports/`. All four compiled report-accounting
fixtures pass after removing a duplicate test-only theorem type in `f593c886`;
that fixture fix leaves production `src/` byte-identical to the tested revision.

The complete repair branch is now fast-forwarded into `../elisa-engine-proof`
(`elisa-engine-proof`) at `f593c886`, including committed main through `a98acb6c`.
A matching official pair publication is running there before the full compatibility
matrix and shared qualification. The old frozen pair remains attached to the
separate ongoing native gate. The guarded-field snapshot task is repaired;
full matrix acceptance and unknown source-control/type contexts remain open.

Integrated clean paired generation `44b744c7ab0b4a50ba2aeca2632099f5` now
authenticates prover `f593c886`, exact Stage1 compiler `52d60fcf` and checked
binary hashes. Its uncached engine sweep passes 73 reports / 4,246 obligations,
zero errors, diagnostics and gaps (3.57s, 164,160 KiB peak RSS); retained reports:
`scalar-copy-integrated-engine-reports/`. The integrated full matrix is running
serially with the original 8 GiB cap and all checks retained.

The initial matrix invocation was deliberately stopped after confirming child
`python3` still resolved to Xcode Python 3.9.6: the Homebrew libexec directory
provides `python` but no `python3` link. It is incomplete, not a compatibility
result (`prover-f593c886-full-matrix.log`; explicit interruption reason:
`prover-f593c886-matrix-python-selection.json`). A task-local `python3` symlink
to the existing Python 3.14 executable is version-checked as 3.14.8, and the
serial retry uses that directory first on PATH
(`prover-f593c886-python314-full-matrix.log`). The earlier scalar-witness
annotation failure is gone: the actual R-019 indexed-vs-linear oracle now passes
with collision, marker, width, duplicate and shadowing controls. The retry and
the independent full native gate are still live; no terminal matrix/native
acceptance is claimed.

### Corpus identity repair and remaining replay failures (2026-10-08)

Isolated prover commit `0e4cd497` repins the reviewed Luna corpus to integrated
source ancestor `f593c886` and updates the changed names-and-scale expectation
source hash. The diff changes two values; expected semantic categories are
unchanged. All 13 manifest controls pass under Python 3.14, retaining stale-hash,
ancestry and malformed-evidence refusals. This commit is not integrated into the
production worktree while its full regression matrix remains active.

Actual paired-product semantic execution still fails `symbolic_quantifier`
(3 replay gaps), `rejected_symbolic_quantifier` (2) and `branch_join` (1).
The branch gap is the line-32 `keep_or_replace` return certificate following
`b: mutable usize = best; if flag: b <- slot`; the assertion remains
`result <= 8`. Reports and unchanged outcome inventory are retained in
`build/validation/luna-corpus-f593c886-reports/`. These source/replay defects
require owning prover repairs; manifest controls alone do not qualify the corpus.

### Rebind-site repair qualification (2026-10-08)

Isolated prover `14ae4a02` rejects an appended alternate reserved symbol for
the same source definition. Harness repair `464b6aab` exposes all selected
private extensions only in temporary test copies; production visibility stays
private. The original adversarial harness now executes all controls. Its
invariant-edit test separately checks initializer source validity and refusal
of an old certificate whose goal no longer matches the source invariant.
A baseline-only diagnostic independently reproduced assertion 110 before
this test correction (`source-binding-baseline-after116-relocated.log`);
that scratch diagnostic bypassed assertion 116 solely to reach the later
control and is not qualification evidence.

The exact committed harness passes in 80.66 seconds, peak 1,233,056 KiB
(`build/validation/source-binding-14ae4a02-exact-harness.log`). The official
paired build completes in 233.91 seconds, peak 1,377,856 KiB. Immutable
generation `f43713c9ff9e4f559d46c4b1ed68331b` records clean source `14ae4a02`
for both products; binary hashes were checked against both manifests.
Its uncached engine sweep passes 73/73 reports and 4,246/4,246 obligations
in 6.10 seconds, peak 165,280 KiB, with zero semantic diagnostics, failures,
trusted assumptions or replay gaps and independent replay enabled. Reports:
`build/validation/rebind-site-engine-reports/`. Both scalar-copy and
next-write control suites pass (`rebind-site-copy-controls.log`,
`rebind-site-next-write.log`).

Test-only follow-ups `00db3e8b` and `c7235dea` preserve the collection
refusals while recording propagated `source-error` and updating the corpus
source/expectation identity. All 13 manifest controls pass. They do not
repair the remaining branch-join or symbolic-quantifier replay gaps.
The full frozen f593c886 matrix remains active; these isolated commits are
not yet integrated into its worktree. Full compatibility remains open.

### Readonly entry-count fallback repair (2026-10-08)

Prover `7d91bb38` closes two fallback bypasses: duplicate entry-count
definitions and a declaration-line cutoff that missed body mutation. The
legacy route now matches the primary route's readonly requirement across
the owner body. This admits readonly entry equality; it does not preserve
equality to the current count through a mutation or alias.
The original O2 source harness passes all seven controls, including
duplicate/forged definitions, push/pop, writes and borrowed aliases
(`build/validation/entry-count-readonly-fallback.log`, 251.38 seconds,
986,848 KiB peak RSS under the unchanged 3 GiB cap).

The official clean paired build publishes immutable generation
`612f602b7ba54af58094a5cbdd37926b` in 152.98 seconds, peak 1,573,376 KiB.
Both source manifests and binary hashes were checked. Its uncached engine
sweep passes all 73 reports / 4,246 obligations in 3.13 seconds, peak
164,992 KiB; all diagnostics, failures, trusted assumptions and replay gaps
are zero, with independent replay enabled. Copies are retained in
`build/validation/entry-count-engine-reports/`.

Subsequent upstream `e27b11bc` literal-product gains were merged before
CLI repair `d961eee0`. That repair restores focused `timeout` from the exact
selected goal's diagnostic; baseline inspection found seven `unknown` versus
`timeout` mismatches and preserved the separate unknown/disproved outcomes.
Its combined product build and regression checks remain pending. The full
frozen f593c886 matrix remains active and none of these isolated changes
constitutes full compatibility qualification.
# Focused timeout classification qualification (2026-10-08)

## Indexed-copy quantified bound repair

Producer follow-up `3b2647c6` transfers receiver-free indexed-copy consequences
after registering the local's scalar/operator witnesses, using the quantifier,
copy equation and receiver-free index bounds. The local keeps its own symbol
when a consequence is established. The original copied-element case now
proves/replays 6/6; all six focused wrong-range, wrong-receiver, pre-copy write,
rebound-copy and stale-cell-equality cases remain refused with zero gaps.
Clean paired generation `ebab2184e65b4b6b99f2dd3cb556fec4` builds in 78.65s at
2,116,160 KiB RSS; both manifest/source identities and actual binary hashes
were checked. Its uncached engine sweep retains 73 reports / 4,246 obligations,
zero diagnostics or gaps, independent replay and no trusted assumptions.
Reports: `build/validation/index-copy-bound-engine-reports/`. The accepted
symbolic corpus now has two replay gaps; rejected symbolic and branch-join
cases retain two and one respectively. Full compatibility is still failed.

Replay follow-up `eb69c836` independently decodes the source range, including
its binder type and empty capture list. It authenticates only the bound from
the unique immutable declaration at the exact copy line, a matching primitive
array element/bound type and a source-exact lower-endpoint range with its strict
upper bound. It rejects primitive aliases, shadowed binders, effectful prefixes,
later copy/scalar writes, trace metadata changes and equality to changed cells.
The compiled gate accepts the authentic bound/range and refuses twelve forged
trace/source/range controls in 20.85s at 1,250,656 KiB RSS under 3 GiB
(`build/validation/index-copy-bound-final-source-controls.log`). Its final
paired product generation `d090da974e63494c91df259890744473` now builds in
73.59s at 2,881,952 KiB RSS. Both manifests identify clean `eb69c836` source;
actual binary hashes match. Its seven focused controls pass, and its uncached
engine sweep retains all 73 reports / 4,246 obligations with zero failures,
diagnostics or gaps, independent replay and no trusted assumptions. Exact
reports are in `build/validation/index-copy-independent-engine-reports/`.

The broader symbolic suite is terminal failed (113.55s, 1,393,696 KiB RSS):
the accepted symbolic case now replays 77/79 certificates, while bubble,
sort and partition failures persist. See
`build/validation/index-copy-independent-symbolic-suite.log`. Fresh old/new
partition comparisons confirm identical unique goal inventories: the changed
raw counts remove duplicate bounds checks produced by substituting already
copied elements into later writes. Original declaration reads remain checked
and replayed. The accepted comparison removes two duplicate rows and the
rejected comparison six; retained comparison artifacts are
`build/validation/partition-index-copy-inventory-comparison.json` and
`build/validation/rejected_partition-index-copy-inventory-comparison.json`.
This is not full matrix qualification; preserve the terminal suite counts and
investigate any further inventory change individually.

## Unused entry-state ghost repair

Producer repair `73e2c25a` seeds entry-count ghosts only when the mentioning
postcondition also uses `old`. Replay's readonly, uniqueness and mutation checks
are unchanged. The pre-repair compiled trace audit exits 101, identifying a
refused local-binding trace. Clean paired generation
`dd326a16639a4f86bd650042a9e0a2dc` builds in 62.04s at 2,247,328 KiB RSS;
both manifests and actual binary hashes match the clean source identity.

The isolated narrowed-write case now proves/replays 4/4. Original collection
push and pop cases pass 20/20 and 16/16; rejected push, stale-count and pop
cases remain failed with zero replay gaps. The corpus's accepted symbolic case
returns to three gaps; the two rejected-symbolic and one branch-join gaps remain
open. The uncached engine sweep passes 73 reports / 4,246 obligations with zero
diagnostics, failures or gaps, independent replay and no trusted assumptions.
Reports are preserved in `build/validation/unused-entry-ghost-engine-reports/`;
build and sweep logs share the `unused-entry-ghost` / `prover-unused-entry-ghost`
prefixes. The owning symbolic suite now includes the isolated regression;
its broader run completed with status 1 in 85.62s at 1,706,112 KiB RSS. The
isolated regression passes, while original symbolic quantifier (three gaps),
rejected symbolic quantifier (two), bubble sort (six), rejected bubble sort
(18) and rejected partition (two) replay failures remain; partition retains
four failed obligations. Exact output is
`build/validation/unused-ghost-symbolic-suite.log`.

The compiled seven-case entry-count controls pass in 55.33s at 2,108,064 KiB
RSS under the unchanged 3 GiB limit: readonly capture accepted; duplicate,
forged, push/pop, indexed-write and borrowed-alias definitions refused
(`build/validation/unused-ghost-entry-count-source-controls.log`).

The carried-alias gap is reproduced independently by extracting the original
`alias_instance_carried` function: six certificates, five replayed, one gap
at its return. Its copied scalar must retain the quantified bound after the
source element is overwritten. Source/report are preserved as
`build/validation/alias-instance-carried-replay.elisa` and
`build/validation/alias-instance-carried-report.json`; this remains an open
source-authentication repair, not evidence of accepted snapshot support.

The compiled return-context trace audit builds successfully in 50.33s at
2,981,328 KiB RSS under the original 3 GiB cap. Its bounded run exits 120,
meaning first refused trace index 20: `local-binding`, line 8, the synthetic
`a == xs[i]` equation introduced at the write rather than the line-7 copy.
Logs: `build/validation/alias-instance-carried-return-trace-audit-build.log`
and `build/validation/alias-instance-carried-return-trace-audit-run.log`.
The repair must reconstruct the receiver-free `p <= a` consequence at the
immutable declaration, authenticate the original quantifier and index bounds,
and expire equality to the overwritten cell. Retain wrong-range, wrong-receiver,
rebound-copy, shadowing, effectful-prefix and forged-trace refusals. Do not
authorize the synthetic later equality merely by moving its source stamp.

## Terminal compatibility result and corpus follow-up

The frozen `f593c886` full matrix completed with status 1 and 54 failed steps:
3,375.53s, peak 7,881,968 KiB RSS under the unchanged 8 GiB limit. Exact terminal
evidence is `build/validation/prover-f593c886-python314-full-matrix.log.json`;
the extracted failure inventory is
`build/validation/prover-f593c886-terminal-failure-inventory.json`. This is a
terminal failed compatibility run, not an observation timeout.

Corpus identity repair `ba5016d8` pins committed ancestor `5016891c` and refreshes
the changed names/scale oracle hash. All 13 identity controls pass; semantic
categories are unchanged. The `d961eee0` pair passes 12 of 15 corpus workloads.
Symbolic quantifiers, rejected symbolic quantifiers and branch joins retain
respectively four, two and one replay gaps (reports in
`build/validation/luna-corpus-d961eee0-reports/`).

The extra accepted symbolic-quantifier gap is isolated to `narrow_past_write`:
the one-function extraction proves/replays 4/4 with `f593c886`, while `d961eee0`
produces four certificates but independently replays only three. The remaining
goal is the quantified ensure after writing outside the narrowed range. Preserve
this regression and its refusal counterparts while locating the owning change;
do not promote the newer pair as fully compatible. Exact comparison:
`build/validation/narrow-past-write-comparison.json`, with source in
`build/validation/narrow-past-write-replay.elisa`.

Clean prover source `d961eee0a231e9f1a8f168c721f18648d89db4bd` built paired
generation `8948585eca3b47bbab0bd7ef83333809` with frozen compiler `52d60fcf`
and its matching runtime. Both manifest identities and actual binary hashes
were checked. The bounded build completed in 111.26s at 1,427,744 KiB RSS.

The exact-goal regression passes all nine timeout/unknown/disproved cases
(`build/validation/focused-timeout-goal-states.log`). The uncached engine sweep
passes 73 reports and 4,246 obligations, with matching certificate/replay counts,
zero failures, diagnostics or gaps, independent replay and no trusted assumptions
(`build/validation/focused-timeout-engine-sweep.log`; preserved reports in
`build/validation/focused-timeout-engine-reports/`). This repairs the CLI's
selected timeout classification without changing certificate admission.

Separately, test commit `5016891c` isolates synthetic Stage1 builds from inherited
`ELISA_*` qualification overrides. All 11 provenance tests pass under the real
qualification overrides, including a deliberately poisoned environment
(`build/validation/mock-stage1-provenance-isolation.log`). Production provenance
checks remain intact. Full compatibility and native release gates remain open.
