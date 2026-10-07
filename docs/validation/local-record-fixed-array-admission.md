# Owned local record fixed-array admission

The bounded fixed-place collector now seeds built-in indexing and exact outer
counts for owned, reference-free local records without borrowed views. It uses
the existing depth-three/64-place collector; scalar-witness budgets stay unchanged.
Nested shapes can repeat one place, so count always takes its first (outer)
dimension, avoiding contradictory count facts. The reference/view audit accepts
record array lengths resolved by the existing unique qualified-constant importer;
unknown or ambiguous extents stay conservative.

A call-initialized local record's guarded write previously replayed 3/4 obligations;
it now replays **4/4**, zero gaps. Nine controls pass both whole-file and function
routes: literal, qualified and nested storage accepted; wrong outer bound, missing
guard, empty storage, ambiguous constants and a borrowed-view record refused.
Existing record-array, late-index and scalar-float controls pass. ActionInput
remains **199/228**, zero gaps; the full uncached sweep remains **69/73**.

Strict O2 paired generation: 3d6ac94692834732bd21b10c3d47837a, using the isolated
exact 8006b660 snapshot described in prover-main-integration-20261007.md.
A prior build refused changed inputs; it was terminal before rebuilding.
Logs in build/validation: proof-local-record-qualified-build-2.log,
local-record-fixed-controls.log, local-record-qualified-sweep.log, and
input-local-record-qualified.json.

Real audio dispatch still needs extent facts to survive captured record mutation.
An isolated direct-extent/local-slot probe changes its refusal from unsupported
indexing to an upper-bound obligation: batch.events.count's equality disappears
from the loop state. Direct constructor locals also need a separate substitution
repair (zeroed fields currently become zeroed.count). Neither engine source was
changed to hide those failures. Probes: audio-local-record-dispatch.elisa/JSON
and local-record-index-fixed.json in build/validation. Full matrix remains open.

## Captured mutation retention

The paired product generation `33cfc686b86e4bc79273392cfb97d74f` retains explicitly traced fixed-array count equalities rooted in a live record binding across captured record mutation. Ordinary runtime count facts still require type-bound provenance.

`python3.14 ../elisa-engine-proof/scripts/test_local_record_fixed_index.py` passes 13 cases through both whole-file and function routes. New controls accept counter mutation before an in-range element write, reject an oversized loop, reject a smaller shadowed record under the outer bound, and accept its own checked bound. The mutation reproducer improves from 3/4 to 4/4 with all certificates replayed and no gaps.

This does not establish AudioAnimEvents completion: the isolated dispatch probe remains 38/42 with four findings, including its index upper bound. Both record-field and copied-slot guard probes still refuse that obligation. No audio production source was changed. Build log: `build/validation/proof-fixed-extent-retention-build.log`; ignored probe reports: `build/validation/local-record-{loop,shadow}-retention-after.json` and `build/validation/audio-local-record-slot-guard.json`.

### Remaining count witness gap

A minimized `examples/local_record_fixed_count_budget.elisa` in the prover repository reproduces the audio upper-bound refusal with an unrelated record containing sixteen scalar fields. The eight-field control proves 4/4; sixteen, twenty-four and thirty-two fields prove 3/4, all without semantic errors. The refused goal retains the slot unsigned marker, guard, builtin index witness and exact fixed count equality; it lacks the array count primitive/unsigned witnesses present in the passing control. This isolates scalar witness budget exhaustion, rather than lost extent retention. Next repair: seed fixed-array count witnesses through the bounded fixed-place collector and authenticate the same source shape during replay, preserving existing scalar budgets. Ignored reports: `build/validation/record-extent-noise-{8,16,24,32}.json`.

The initial count-witness patch used `proof_add_scalar_type_witness`; the paired build completed but the new budget regression still refused 3/4 because that helper shares the exhausted 32-fact function budget. The revised producer adds only the primitive and unsigned witnesses for counts reached by the existing bounded fixed-place collector. Scalar budgets remain 16 per walk and 32 per function; the fixed collector remains capped at 64 places. This revised patch is pending qualification; its build log is `build/validation/proof-fixed-count-witness-independent-build.log`. Existing replay treats type-bound traces as source-boundary facts, with structural payload validation; this patch adds no new replay rule.

### Independent fixed-count witnesses qualified

Generation `726a00ef599f4790b19c18d66870d27a` completes the revised strict O2 paired build. The local-record suite passes 16 cases through whole-file and function routes, including budget exhaustion acceptance, oversized/missing guard refusal and smaller-record shadowing. Record-array (8), fixed-index admission (7), and scalar-field-priority (5) controls also pass. Scalar budgets and the 64-place fixed collector remain unchanged.

The engine dispatch now uses its direct animation capacity constant, a checked local slot, and refusal before writing; the queue remains intact on refusal. The real `proof/audio_anim_events.elisa` report improves to 55/58, zero replay gaps and three findings (animation tick operator, play resource summary, tick function summary). The added guard contributes an obligation, so the complete inventory is 58. Report: `build/validation/audio-anim-count-witness.json`. Runtime qualification is running in `build/validation/audio-count-witness-runtime.log`; it is not yet claimed passed.

Runtime qualification completed successfully: `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 python3.14 scripts/run_tests.py ~/.elisac/elisac-stage1 --no-cache -j4` passes all 215 tests, zero cached compiles, status 0 in 40 seconds. Log: `build/validation/audio-count-witness-runtime.log`. This validates the audio write change but does not close the three remaining animation summary findings.

### Scoped animation blend operands

`Playback::tick` now scopes its two cast results as explicit `f32` operands in `region blend_scope`. The expression preserves the same cast/division and allocation lifetime. Direct cast-call division was rejected as possibly overloaded; its typed local operands admit builtin division without a float arithmetic theorem. The real audio report is 55/56 with all 55 certificates replayed and zero gaps. Removing unsupported expression findings changes the obligation inventory, so this is not reported as the earlier 58-row inventory.

All 215 uncached runtime tests pass, status 0, in 34 seconds (compiles done at 33 seconds). Log: `build/validation/animation-scoped-blend-runtime.log`; proof report: `build/validation/audio-anim-scoped-blend.json`. The remaining anim_play resource-summary refusal reproduces in `examples/plain_enum_resource_call.elisa` in the prover repository: a verified setter takes a mutable state reference and plain enum value, and its forwarding caller is refused (1/2, no semantic errors). This remains a prover task; no enum conversion or state ownership workaround was applied.

### Audio proof closed by plain enum storage classification

Paired generation `66d66eb925ca41349fce8e12bc3520b6` classifies uniquely resolved zero-payload enum parameter targets as reference-free for resource-summary confinement. Hierarchies, payload enums and ambiguous aggregate names remain conservative. It adds no enum arithmetic rules or resource replay shortcuts. Five new cases pass through both whole-file and function routes; all 16 local-record and 11 float-resource controls also pass. The new suite is wired into the prover matrix.

The real AudioAnimEvents proof now passes 56/56, zero findings, zero semantic errors and zero replay gaps (`build/validation/audio-anim-enum-summary.json`). The full uncached engine sweep passes 70/73; only ActionInput context, ActionInput deadzone and SoundAssets remain (`build/validation/proof-plain-enum-engine-sweep.log`). The full implementation plan and toolchain matrix qualification remain incomplete.
