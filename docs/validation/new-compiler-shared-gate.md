# Shared gate on compiler 96761822 — 2026-10-07

The initial fresh `scripts/check.elisascript` run failed five GLB-related test
compiles because `GlbJson::Token.stop`, `size` and `after` were declared
immutable but assigned while constructing tokens. Those three field types now
declare `mutable usize`. Tokenization logic and bounds are unchanged.

The next run passed all 214 Elisa tests plus SDL3 and headless Godot probes,
then correctly rejected stale Character Course rig outputs. Regenerating via
`/opt/homebrew/bin/python3.14 examples/character_course/make_rigs.py` updates
the committed `.pkg` and `.anim` to the repaired global skin space. Its
`--check` now passes. The earlier relocated package contains the pre-refresh
guide files; its retained hashes describe that exact historical bundle.
A final package refresh is required after the shared/native gate work.

The subsequent full check passed 214 tests, SDL3/Godot compatibility, native
unit tests, interactive-validator tests, source policies and viewport/navigation
boundary tests. It stopped at the proof stage: 73 proofs, 64 cached successes,
nine executed failures. This is not a passing shared gate.

Failed proof rows:

- `action_input_context`, `action_input_deadzone`
- `application_capture_timing`
- `audio_anim_events`, `audio_music`, `audio_triggers`, `audio_virtual`
- `motion_overlay_policy`, `sound_event_assets`

Examples: action/audio checks report unsupported kernel proposition formation;
capture timing reports replay gaps; sound-event parsing has an unpreserved
loop invariant and an unverified callee summary. Do not drop these proof rows
or substitute test results for their acceptance. The plan requires fixes in
`../elisa-engine-proof`, merging committed main/wasmbrowser gains first and
retaining accepted/rejected regressions. Existing GLB layout proofs remain in
`proof/glb_layout.elisa` and `proof/glb_layout_index.elisa`; their cached success
does not prove all tokenizer behavior. This field-mutability correction adds
no new arithmetic policy or claimed formal coverage.

Logs:

- `build/validation/shared-check-96761822-2026-10-07.log`
- `build/validation/shared-check-96761822-glb-fix-2026-10-07.log`
- `build/validation/shared-check-96761822-rig-refresh-2026-10-07.log`
- `build/*-proof.json` for the individual reports

The compiler's required hosted-CI machine-over fix (`955cde86`) is now an
ancestor of published compiler main `96761822`; the old unpublished-compiler
blocker must be revisited after compatible proof/ElisaScript provisioning is
verified. Hosted CI execution and the full native application gate remain open.

## Fresh prover sweep after enum admission

Prover commits `a10c784f` and `d3609076` bind recipe validation to the
pinned compiler's inventory and admit checked ordinary enum equality and its
Boolean result. The clean committed prover was rebuilt with compiler
`96761822`. Five focused enum cases pass: one accepted with complete replay,
and four rejected cases covering foreign ownership, direct and alias equality
overrides, and an invalid comparison claim.

An uncached run of all 73 engine proofs on that binary passes 64 and fails
the same nine rows listed above. This establishes fresh coverage for the 64;
it does not close the shared gate. The capture-timing report isolates its
first gap to replay of `whole * 1000000000 + capped` under the recorded
upper bounds; two caller gaps depend on that unverified function summary.
Its runtime implementation and contracts have not been weakened.

Logs: `build/validation/prover-d3609076-clean-build.log` and
`build/validation/proof-d3609076-full-sweep.log`. Reports remain in
`build/*-proof.json`.

## Capture timing replay restored

Prover commits `8fb5bb87`, `c85737cb` and `fd5518b6` reconstruct immutable
conditional equations, captured local reads and independently checked literal
constant rebinding. Seven conditional regressions and five enum regressions
pass, including invalid bounds, mutable inputs and constant shadowing.
The actual engine capture-timing proof now proves and replays all 11
obligations, with no findings or semantic errors. Its implementation and
contracts remain unchanged.

The latest uncached sweep passes 65/73. Eight failures remain: the two
ActionInput rows, four audio rows, motion overlay policy and sound-event
assets. The shared gate and full prover regression matrix remain open.
Evidence: `build/validation/proof-conditional-constants-full-sweep.log` and
`build/application-capture-timing-proof.json`.

## Broader prover compatibility audit

Prover `671ec265` restores audio-music replay (35/35 obligations), bringing
the uncached engine sweep to 66/73. The subsequent all-products build at
`2a0d0804` completed, including portable replay. Its serial KEEP_GOING full
prover matrix finished with 98 failed shell steps, including harness cascades.
These are broader compatibility gaps; the engine sweep does not establish
whole-prover compatibility. Build fixtures and provenance fixtures have since
been repaired with focused passing tests; source/replay and mutation-harness
failures still need triage. The native/shared gates remain open.
Log: `build/validation/prover-2a0d0804-matrix.log`.

## Source-bound frame certificate groundwork

Prover `753b49f5` builds all products with compiler `96761822`. Frame replay now
has canonical arena places/policy predicates, exact source owner and formal type
reconstruction, independent header/body policy decoding, nominal field checks,
exact direct write matching and composed fact-free source checks. Allowance and
preservation remain distinct predicates so their existing events and diagnostics
can be retained. Focused adversarial controls pass at O0/O2; the full arena runtime
passes at O0, source admission refuses six malformed classes on twelve routes,
and the kernel inventory matches ten tables / 188 entries.

This does not close static frame obligation accounting: source spec/preservation
binding and per-event producer/certificate admission are still pending, as are
qualified/generic type paths and nested/alias/dynamic/callee mappings. The fresh
uncached engine sweep remains 66/73 with the same seven failing rows. The full
prover compatibility matrix and engine shared/native gates remain open. Evidence:
`build/validation/prover-frame-allowance-build.log`,
`build/validation/proof-frame-allowance-sweep.log`, and the prover's
`docs/validation/obligation-attempt-accounting-gap.md`.

## Source-bound direct frame CLI accounting (2026-10-07)

Prover 99527e39 integrates complete supported source frame inventories into
producer attempt/certificate recording and independently source-bound CLI replay.
The previous three minimized accounting failures now preserve all original
events with certificates: allowed write proves 3/3; outside write rejects with
3 events/2 certificates; preservation violation rejects with 5 events/4
certificates. Failed events remain goal-linked attempts/findings. O0/O2 runtime
controls, full/summary JSON regressions, malformed-source admission matrix,
invariant diagnostics and 193-entry kernel inventory pass. All-products strict
O2 build succeeds, generation fc86464d1b494b06b1b5e824f4701d47.

The fresh uncached engine sweep remains 66/73 with the same seven failures.
Contract-placement and condition-call-position examples still fail admission
accounting because broader source event mappings are incomplete. Nested, alias,
dynamic and callee effects, portable source-context replay, the complete prover
matrix, compiler qualification and full engine plan remain open. Evidence:
build/validation/prover-frame-cli-build.log and proof-frame-cli-sweep.log;
prover docs/validation/frame-report-source-correspondence.md.
