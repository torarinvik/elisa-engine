# Compiler 50b16e68 installed; engine qualification pending

The UI chat installed upstream revision
`50b16e6800fe03e3369a78aa85fffb55ee2f38e3` on 2026-10-07. The engine chat
independently read the installed SNAPSHOT and ran its source freshness check:

```
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash /Users/torarinvikbjarko/.elisac/stage1/scripts/assert_stage1_fresh.sh /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1
```

Exit zero. SHA256 of installed compiler:
`a0a9b2951d68c64f3a3c56cf20ff99eee8256af3f0ff467c591cecaac6eb1bae`.
Runtime: `ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.
This revision adds audited borrow-exclusivity checks and reference-field
backend lowering. Its engine performance has not been measured.

The mocap branch bridge commit `3c51102b` passes strict extern declaration
compilation for five bounded FBX entry points, plus one linked call at O0/O2
on this compiler/runtime pair. Native boundary, staging, actual conversion,
digest/cache and source/existing-output preservation checks pass. Exact
header and evidence are in that branch's `native/studio_fbx_bounded.h` and
`docs/validation/fbx-bounded-bridge.md`; client staging/cache remains there.

This focused check does not establish shared/native engine gates, a refreshed
prover product pair or the full prover matrix under 50b16e68. Those must be
rerun with matching archived parser/runtime inputs. Earlier 63585c5f evidence
remains historical qualification of that specific pair, not this installation.
Hosted pins remain unchanged pending the complete gates.

## Engine qualification results

Strict O2 prover pair generation `de12689f110f42228c452db5c13a2cef` built
from prover `bd2e036b`, using installed 50b16e68 compiler/runtime and matching
archived compiler/parser sources. Both products completed with frozen sources.
Build log: `build/validation/prover-50b16e68-build.log`.

Fresh engine tests: 214/214, zero compile-cache hits, four workers, 44 seconds.
`build/validation/engine-tests-50b16e68.log`. This is compatibility evidence,
not a controlled performance comparison with the previous compiler. Native
unit tests also pass (`native-units-50b16e68.log`).

Focused prover checks pass: seven decimal-for saturation controls, four
captured initializer controls, six mutable-for entry controls, seven existing
captured entry controls, seven source-call alias controls, six stable predicate
controls, four literal-quotient controls, malformed source admission (six classes
on all twelve routes), and kernel inventory (10 tables /194 entries).

Uncached engine corpus: 73 run, 69 pass, four fail.
`build/validation/proof-sweep-50b16e68.log`. Exact outstanding reports:

| Proof | Proven / obligations | Replay gaps | Findings |
| --- | --- | --- | --- |
| action_input_context | 31/74 | 0 | 44 |
| action_input_deadzone | 31/74 | 0 | 44 |
| audio_anim_events | 45/58 | 0 | 13 |
| sound_event_assets | 153/157 | 4 | 0 |

The two ActionInput reports additionally contain four semantic diagnostics,
but zero semantic errors. Producer findings and replay gaps are distinct: the
first three failures need proof coverage, whereas sound assets needs source
replay reconstruction. None of these failures was waived or removed.

Full shared command used `elisascript scripts/check.elisascript`, explicit
installed compiler/matching prover, Python3.14, CommandLineTools, Wicked root
and forced audio-device-unavailable policy. Terminal exit1 at the proof stage.
All earlier stages pass, including cached rerun of214 tests, SDL3, Godot
compatibility, native units, scene/skin/localization artifact checks, Metal
viewport render/motion, navigation and owner/privacy rejection fixtures.
Log: `build/validation/shared-check-50b16e68.log`.

The complete prover regression matrix, full native application gate, refreshed
package and hosted execution still remain open. Shared gate remains red at
proofs; focused compatibility does not promote hosted pins.
