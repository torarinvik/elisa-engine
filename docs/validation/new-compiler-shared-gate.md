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
