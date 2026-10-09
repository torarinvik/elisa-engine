# Global mutable identity grants

## Required compiler policy

The compiler must enforce `Global.Read` for reads of `global mutable` values
and `Global.Write` for writes by default. A read-modify-write needs both.
`-permissive` bypasses these grant checks. Default enforcement, selective grants,
qualified names, shadowing, indexed/member writes and transitive calls require
compiler-owned positive and negative regressions. An earlier installed compiler source
`4655dbaa17131bcb158be69d5b4939bf64eb6bea` and Stage1 product
`cfcdc1fa99752f417c4267f125ed3419be26e569115fb28df3027095cb7e1da2` pass the
Global authority controls, and the matching engine/runtime gate passed 216/216
on that earlier tuple. The exact current b11e evidence is below. Proof-pair and
Studio acceptance remain open.

## Prior candidate and source-boundary evidence

The `12120f6b` compiler candidate, superseded milestones, native-boundary audit,
identity-counter grant adoption and frozen-source qualification are summarized in
[the consumer history note](global-mutable-grants-consumer-history.md). These
older product results do not qualify the current b11e Stage1 or its proof/Studio
acceptance.

## Focused qualification

On the frozen compiler `52d60fcf` with its matching runtime, direct executable
compilation and execution of `test/world.elisa`, `test/world_storage.elisa`,
and `test/world_access_serials.elisa` pass all existing assertions. Command:
`python3.14 build/validation/qualify-world-global-grants.py` under the existing
3 GiB / 360 second watchdog; 1.68 seconds / 111,392 KiB peak RSS.
Log: `build/validation/world-global-grants-runtime.log` and watchdog JSON.

An uncached sweep using verified clean prover generation
`a0ea428da32c4674aa411bb4d0243540` retains all 73 reports / 4,246 obligations,
all proved, certified and independently replayed, with zero errors, diagnostics,
gaps or trusted assumptions (1.87 seconds / 161,952 KiB, original 3 GiB cap).
Command: `python3.14 build/validation/check_world_grants_engine.py`.
Exact reports and inventory: `build/validation/world-global-grants-engine-reports/`
and `world-global-grants-engine-inventory.json`.

This frozen compiler predates default enforcement. The qualified candidate below
has the required default/permissive behavior in focused compiler controls, but this
does not qualify the integrated engine/Studio build or full native compatibility.
Do not use `-permissive` for ordinary engine qualification. The intermittent
effect-memory release failure remains open.

## Historical compiler candidates and toolchain gates

Rejected candidate reports, actual-CLI gate design, product-drift controls, and pre-adoption Studio evidence are preserved in [global-mutable-grants-toolchain-history.md](global-mutable-grants-toolchain-history.md).

## Engine adoption on the current Stage1 — 2026-10-08

The current compiler source `0b43cbdbd04314fcccbdad361bc671196b1ffb1b`
adds explicit global effect rows to the JSON standard library. Its freshly
provenanced Stage1 product is
`/private/tmp/Elisa-compiler-prover-method-rehome/bin/elisac-stage1`, SHA256
`5198034700383a76aa25ca7db2e2e31bdd4fa98753f721b53c80233e24257916`, with
matching runtime object SHA256
`17a5e88040dbe3f13c0ec70b31c7a0bfab57ad89b5e633760d654260f2dde414`.

Using that product without `-permissive`, semantic checks pass for both
`examples/maze/capi.elisa` and `test/viewport_gizmo.elisa`. The C-ABI sample now
declares read/write grants on its stateful accessors and session operations,
read-only grants on backend profile queries, and propagates the required rows
through lazy game initialization. The viewport test's callback counter access
and its callers declare read/write grants. The compiled viewport test runs with
exit 0, and the maze sample emits a C archive successfully.

Retained artifacts and hashes:

- `build/validation/global-grant-engine-adoption/viewport-gizmo-test` —
  SHA256 `4a742fdb7063e026bd5b840df90ebaa47625ac948a4cf052e921627063b7c9d7`.
- `build/validation/global-grant-engine-adoption/libelisa-maze.a` —
  SHA256 `005549e329d6cdf782efb241f14c2f03ea36633e4e6c3efd6627d1fcc5a9eb9b`.

This qualifies the two affected engine targets on the exact compiler/runtime
pair; it does not establish the complete engine, native renderer or consumer
release gates.

## World identity-counter grants — 2026-10-08

The world epoch, storage catalog-brand and access-frame identity issuance
boundaries now declare `Global.Read` and `Global.Write`. Their existing caller
tests declare the same requirements where they construct a `World`, construct a
`Catalog`, or begin an access frame. Counter bounds, exhaustion errors and
identity algorithms are unchanged.

With default-enforcement Stage1 from source `0b43cbdb`,
`test/world.elisa`, `test/world_storage.elisa` and
`test/world_access_serials.elisa` each compile and run with exit 0. The compiler
suite also passes 52 grant controls and six explicit `-permissive` bypasses.
Qualification records, compiler/source/binary hashes and per-test logs are in
`build/validation/global-grant-world-0b43/qualification.json` and adjacent
files. Invoke the compiler from its own checkout so it finds the matching
runtime object.

This closes only the identity-counter slice. It does not qualify every engine
target or the consumer's full check; those default-grant integrations remain
open.

## Shared runtime and Character Course source adoption — 2026-10-08

The stricter compiler exposed grant requirements across stateful audio events,
decision-session storage, action-input recording/runtime, plural formatting,
world save/load, and their callers. The shared functions now declare
`Global.Read` and `Global.Write`; Character Course propagates those effects
through its gameplay helpers and included self-test functions. Legacy native
test entrypoints touched by the same compiler census also declare their caller
effects. These declarations preserve behavior and make the global authority
requirement visible in function signatures.

The Character Course hidden self-test application builds and links with default
grant enforcement using compiler source `dd8aea2281dd2c2755331d4037c17e5d5db92671`
and Stage1 SHA256
`a95da6ad1daee9aab219047782ec04ca5ecb3d063f653f9a2e06b3cdf2a626a3`:

```sh
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/bin/elisac-stage1" \
python3 scripts/elisa_build_run.py build \
  --project examples/character_course \
  --main self_test_main.elisa \
  --output build/validation/global-grants-character-course-self-test
```

The build completed archive generation in 87.63 seconds and native linking in
15.27 seconds. The binary SHA256 is
`5d0dc493d4fb394fb7a1b275aeda1ed7017bf3c408ad21e628df71fcee6c6a92`; its build
identity is `3fec82a0134d4444`. This is compile/link evidence only; the binary
was not launched, and the complete consumer check and relocated startup remain
open.

The ordinary public entrypoint `examples/character_course/main.elisa` declares
the same grants when it calls `CharacterCourse::run`. The dedicated
`live_input_test_main.elisa`, `relaunch_main.elisa`, and `stream_test_main.elisa`
entrypoints carry the same contract. Project-context checks that include
`src/runtime/public.elisa` pass for all three; direct root compilation without
that project wrapper is not a valid check for these client files. Current Stage1
command: `elisac-stage1 -emit check` over a wrapper that includes
`src/runtime/public.elisa` and each entrypoint. The three exit 0 results with
zero grant diagnostics are retained in
`build/validation/global-grants-course-entry-checks.log`.

## Provisional Studio engine diagnostics — 2026-10-08

The Studio project preflight logged 71 apparent grant gaps in 12 engine
animation, asset and viewport modules. Source review found no `global mutable`
declarations in those modules; representative flagged operations such as
`LongClip::read`, `LongClip::set_local` and `GizmoPolicy::next_state` use only
their explicit arguments and immutable constants. The compiler owner traced a
likely false-positive cause to effect-call lookup by leaf function name across
modules. No grants were added from this census. Re-run the exact project check
after the compiler owner-identity repair, then add only effects confirmed by
resolved calls or direct mutable-global access. The 71-row input census and its
log hash are retained in the ignored
`build/validation/engine-global-grants-studio-census.txt`; this is provisional
diagnostic evidence, not a source requirement or acceptance result.

The ordinary entrypoint builds optimized and packages as a compiled-shader-only
macOS app from clean engine commit `7404f2d2534cf30656976d1c3aeaaaac1d28b177`
with compiler Stage1 above and runtime object SHA256
`70e2397f260b42c11f6478a70b0baf95ae09d654ac6b1dfe99bb9f9a4e896de5`. The
executable SHA256 is
`6f5354e393ae8202de5f0856b4e21615f7f451feb86dc650375ba465dcc58671`, build
identity `629e2167d39eab9e`; package size is 127,448,725 bytes with 81 verified
notice files. Elisa archive generation took 126.49 seconds and native linking
took 40.12 seconds. This establishes compile/package integration only; relocated
offline startup, restart and teardown have not yet been run.

This integration also corrected `scripts/elisa_build_run.py`: the build runner
invokes the selected compiler binary directly, so it now passes an empty
`ELISA_RUNTIME_OBJ` to omit the bundled runtime. The `none` sentinel is translated
to an empty path only by the Stage1 shell wrapper; passing it directly made the
compiler request an archive member literally named `none`.

## World scheduling and save-swap callers — 2026-10-08

`WorldSchedule::begin_frame` now propagates the access-frame identity grants.
The save-swap staging and replacement APIs propagate the `World` constructor's
identity grants. The world-command, event, phase-iteration, save, save-swap and
save-rendering tests declare those caller effects explicitly; their behavior
and error handling are unchanged.

All six targets compile and run with exit 0 against the same default-enforcement
Stage1 product. Per-test source/binary hashes and logs are recorded in
`build/validation/global-grant-world-0b43/core-world-qualification.json` and
adjacent files. This qualifies the core world scheduling and persistence slice;
the broader engine grant inventory and native consumer remain open.

## World-identity caller propagation — 2026-10-08

The source audit found additional fully qualified callers of the three
global-backed identity services (`World::World`, `WorldStorage::Catalog`, and
`WorldAccess::begin_frame` through `WorldSchedule::begin_frame`). Their owning
functions already declare the grants; missing caller contracts are now declared
in the maze, environmental-effects, and Character Course source paths and in
the affected world, prefab, audio, render, streaming, and native test entrypoints.
This changes only effect contracts, not runtime behavior.

The current strict Stage1 product is compiler source
`dd8aea2281dd2c2755331d4037c17e5d5db92671`, SHA256
`a95da6ad1daee9aab219047782ec04ca5ecb3d063f653f9a2e06b3cdf2a626a3`. Without
`-permissive`, all 26 changed Elisa source files pass their direct or required
project-context checks (21 direct, five wrapped with the public runtime API and
test support). Project entry checks pass for maze, environmental-effects,
cell-streaming, hierarchy-render, world-picking, world-audio and physics-cadence
paths. Commands and per-source results are retained in the ignored
`build/validation/global-grants-world-caller-project-checks.log`.

The same compiler product rejects all five current Character Course project
entrypoints on four sound helpers (`start_injected`, `reset_scene`,
`restart_music`, and `music_stream_test`). Source review finds no global mutable
binding in those helpers or their audio API callees. Compiler source inspection
confirms the current caller census reduces calls to leaf names before looking
up effect rows, allowing unrelated same-name functions in other modules to
contaminate the result. Do not add grants to these helpers to silence that
census. An exact-callee compiler candidate now passes its focused owner and
grant controls; rerun these entrypoints and the 71-row Studio census after its
promotion. The optimized
Character Course package has not been rebuilt from this caller-contract source
revision, and relocation acceptance remains open.

## Promoted exact-callee compiler and FBX bridge check — 2026-10-08

The exact-callee authority repair is promoted at compiler commit
`42fd1cbee38293b1f7c66a9a6b05eab58b3437c6`. Its qualified integration worktree
passes freshness assertion `scripts/assert_stage1_fresh.sh bin/elisac-stage1`.
Stage1 SHA256 is
`599b3f762db05dba698af0818d3af331387ebcbac64be2f1b137cb0811d97133`; paired
runtime SHA256 is
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
The focused qualification passes 103 mutable-global authority cases,
Stage0/Stage1 inferred-row parity, grouped-effect formatting, CLI grant behavior
and the independent strict `Unsafe.MutableGlobal` capability. The broader Core
fast suite is not yet green: legacy backend fixtures need explicit local
`Global.Read` / `Global.Write` grants, and semantic expectations still require
the old warning text. The standard Stage1 snapshot from commit `42fd1cbe` is now
installed at `~/.elisac/stage1`; its SHA256 is
`71df842ce3fef2e456337d0d1b2c8da9082ca43a0a022d098701aee833825615`. Its paired
runtime SHA256 is
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`, Stage2
self-host SHA256 is
`bace248c8b6ed7fbee1644de0c787749e3f963637f1a9dcd4b48f20db30e35fe`, and the
refreshed Stage0 SHA256 is
`f02d1bd1d255fd8723ba460be6794712c35ced8c3bc6f51ba321c9399764b032`. Freshness
and focused grant/format smokes passed; the exact snapshot was handed to the
proof agent for Linux-native qualification.

On consumer checkout `../elisa-engine-mocap` at `1e98f075`, the strict
default-grant `scripts/test_fbx_bounded_bridge.py` regression passed with
`MOCAP_FBX_ANIMATION_FIXTURE` set to the supplied high-block FBX and the paired
Stage1/runtime above. It passed compilation, staging/hash/cache, bounds and
alias refusals, source preservation, and the shared multi-key channel clock.
No `-permissive` flag was used. The temporary GLB and detailed output are
removed by the script after the run, so this establishes the bounded conversion
contract and shared grid only. It does not establish fresh-app playback,
constant-channel behavior, repeated redraw, the complete Studio grant census,
or relocation.

The consumer owner replayed an already-loaded 337-frame take in an open Studio
instance through frame 248 (~74%) without a crash. The on-disk bundles used in
that check predate the skin-cache fix, so the pass does not validate that fix.
It is not a fresh import or a qualification of the installed `42fd1cbe`
compiler/app tuple.

The current installed Studio bundle is stale: its recorded compiler is an older
dirty checkout and its project revision predates current consumer source. The
earlier temporary `d73f2cf3` semantic preflight logged 10,017 grant-related
diagnostics; after the generator fix, a current-source preflight at
`build/studio-build.5iL67X/semantic.log` used compiler source `42fd1cbe`, Stage1
SHA256 `5f3735d2503f4add196f64ac35af10bdcdbc8a49e1ad5749819642057dfc52ed`, and
runtime SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
The generated icon module has zero diagnostics. The remaining log has 2,253
global-grant diagnostics across Mocap Studio/core source (2,099 in Studio
modules and 154 in other Mocap source) plus 47 UI contract diagnostics (31
global-grant findings and 16 Painter protocol capability findings). Separate
return-flow and runtime type errors also remain, so no fresh app was produced.
The full current Character Course and Studio censuses remain open. The old 71-row
Studio census and four Character Course sound-helper findings were provisional
same-leaf results; exact callee ownership is fixed, so rerun both censuses
sequentially against current sources before adding effects. Do not add blanket
grants from those old reports.

After this recorded preflight, the compiler checkout changed in
`elisacore_std/elisacore_json.elisa`: six parser entry points now declare
`Global.Read` / `Global.Write`. A matching Stage1 product was rebuilt with SHA256
`f5f36a9f726355984382a3aa84f63f8653e0e68b9546778877b85c2906bd0ddb`; its
provenance check passes against the edited source tree, and its runtime SHA256
remains `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
The next semantic preflight at `build/studio-build.eZoCyo/semantic.log` records
1,260 Global-grant findings and 34 UI capability findings after the source
repairs listed in the execution plan. The 2,253 / 47 totals remain the prior
snapshot baseline; the full Studio grant census and current app acceptance are
still open.

After source commit `509ebc21`,
`build/scene-character-check-current.log` reports zero grant diagnostics in
`src/studio/app/scene.elisa` and `character.elisa`, with 1,239 grant findings
remaining in the full Studio closure. Regression source commit `aa9f018c` adds
the caller scopes to `test/studio_character.elisa`; the following executable
compile reports zero direct test-file findings, then stops on 191 dependency
findings across 19 files. The largest dependencies are `src/physics/rig_physics.elisa`
(36), `src/ops/rig_legs.elisa` (35), and `src/studio/app/model.elisa` (33).
That test has not executed; clearing its dependency closure is now the nearest
measurable acceptance step, while app-wide grants and UI protocol errors remain.

## Generated Studio icon grant boundary — 2026-10-08

The current semantic log attributed 383 errors to generated calls from
`StudioIconPaths::draw` into `StudioDraw::segment`. The generator already emitted
the `Global.Read` / `Global.Write` function effect row but did not establish a
local permission scope. `mocap-cleaner/tools/svg_icons.py` now emits
`can Global{Read, Write}:` around the generated body. Its existing
`test/studio_icons.elisa` fixture now declares local grants around its direct
global reads/writes and caller, preserving the original icon bounds and command
count assertions. Both source and test changes are in consumer commit
`dfe41779a065b614dedb63c2eca6caa895cda0bf`.

With installed Stage1 from `42fd1cbe` and its paired runtime:

- `python3 tools/svg_icons.py build/generated/studio_icon_paths.elisa` generated
  30 icons / 383 segments.
- Installed Stage1 SHA256
  `71df842ce3fef2e456337d0d1b2c8da9082ca43a0a022d098701aee833825615` passed
  `-emit check test/studio_icons.elisa` with no diagnostics.
- The optimized test executable built with `-O2` and ran successfully, printing
  `studio icons: all codes draw inside their box`.

This closes the generated-icon local-grant gap only. The full Studio and UI
semantic census and fresh app acceptance remain open; the 10,017-row preflight
log predates this generator fix and must be rerun on the installed tuple.

## Physics regression grant closure and backend emission blocker — 2026-10-08

Consumer commit `1c3dea3b` (`Grant globals in physics rig call paths`),
integrating isolated patch `3da1c5ba`, scopes
`Global.Read` / `Global.Write` around calls in `src/io/rig.elisa`,
`src/tools/limb.elisa`, `src/physics/contact_inputs.elisa`,
`src/physics/balance_frames.elisa`, and the existing effectful helpers in
`src/physics/rig_physics.elisa`. It includes the corresponding
`test/physics_rig.elisa` caller grant in the same source-and-test commit. The
commit is now in the consumer's main history. The pre-scope comparison used
parent source `ecc743c6` from the isolated branch.

At the time of this focused check, compiler source `42fd1cbe`'s Stage1 (SHA256
`f5f36a9f726355984382a3aa84f63f8653e0e68b9546778877b85c2906bd0ddb`),
`-emit check test/physics_rig.elisa` passes with no diagnostics, clearing the
focused regression's grant-checking closure.
`-emit exe` does not produce an executable: the backend declines three return
statements, in `balance_frames@12`, `accumulate_residual@11`, and
`merge_residual@39`. To test whether the new local scopes caused this failure,
the same executable command was run on the original isolated patch's parent
`ecc743c6` with `-permissive`; it produced the identical three declines. The backend failure
therefore predates this grant slice. The runtime physics regression has not run.
## Updated compiler candidate and current consumer check — 2026-10-08

The return-emission repair is committed in compiler `891d2d30ac544245de89a0758e78c6720e94d068`.
It adds `test/parity/void_return_ensure_smoke.sh`; the compiler owner reports the
smoke passes at O0 and O2. The current Stage1 candidate SHA256 is
`ef71c5298815fae721a31082faea6002a247a808dea6420af6a744be0203a359`, paired
with runtime SHA256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`. The
compiler checkout still has a local `elisacore_json.elisa` grant edit, so this
candidate is source-matched to that working tree but is not yet a full compiler
qualification. The previous `8110c2c6` candidate is obsolete for new checks.

Mocap source adoption has since removed the earlier physics backend declines
from the compiler path, but `physics_rig` has not been emitted or run on this
candidate. The worker callback syntax, explicit worker returns, session-worker
visibility and current Stage1 runtime type issue were repaired across successive
snapshots. The return-repair check cleared all 49 fallthroughs. The next
semantic snapshot,
`mocap-cleaner/build/studio-semantic-after-returns.elisa.log`, reports 87
effect/protocol/lifetime findings: 40 in Mocap (mostly scoped calls and direct
global accesses) and 47 in UI. UI's largest cluster is 34 `Painter` /
`DiagnosticMaskPainter` protocol effect mismatches; other UI findings cover
effectful text-measure callbacks, Core event grants, a contract-order error and
a global-backed result lifetime. This snapshot followed the return fixes, but
it is not a successful build: no fresh Studio app or `physics_rig` executable
has been qualified on this tuple. Earlier 123-, 138- and 49-row snapshots are
historical.

Next acceptance: after the consumer owner repairs its current source and
explicitly releases the shared compiler slot, build/check the current Studio
source, then emit and run `physics_rig`, followed by the latest Studio character
dependency census sequentially on the same candidate and consumer snapshot.
Earlier 1,239 / 191 counts and the intermediate 123 count are historical only.

## Follow-up compiler/backend integration — 2026-10-08

Compiler commit `35479edc` changes the standard-library `join` result retrieval
to a direct generic helper call and adds a runtime regression for scalar and
32-byte aggregate results. The compiler owner reports the focused regression
passes at O0 and O2 on a fresh seed. The exact Stage1 product SHA256 for this
commit has not yet been recorded; the earlier `f4c9d54c…` product belongs to
compiler source `34574304` and must not be reused as current provenance.

A build-only Studio compile against `35479edc` now declines 17 backend
constructs, down from the earlier 25. Remaining diagnostics include the generic
`ctx_concurrency_result_read` helper and its indexed `Result` specializations,
the `generation_scan_root` global initializer, five generic call expressions,
and the AppKit file-drop callback. This is diagnostic-only output: there is no
current-source app bundle. The saved crash trace is attributable to
`build/studio-package.cO7zyV/previous.app`, whose recorded project/compiler
revisions are `31459ca3` / `e24c29e6`; it neither qualifies nor contradicts the
current source. Keep the strict proof freshness and exact runtime/product
identity gates unchanged while the 45 compiler-source grants and remaining
backend declines are resolved. This 45-count summary is historical and
incomplete: the captured compiler diagnostics were truncated, so the count and
the listed three-file split must not be used as the current migration inventory.

## Latest compiler products and grant-adoption status — 2026-10-09

Compiler main is clean at `ec41ca95` (`Resolve export targets past effect
metadata`). Its fresh Stage1 product SHA256 is
`acc5c27caf0fae47b39d0bba0b870b5d29f218c18f50130400b7e462bf01c526`, and the
matching runtime object SHA256 is
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`. The
Stage1 provenance check passes for source-tree hash
`391012beba8144c36eb131598f94db5d80bfc9069643c3dec368025c86c11e44`. The
corrected actual-CLI gate passes 24/24 controls on this exact compiler product,
including default read/write and read-modify-write refusals, local grants,
signature-versus-body authority, and the explicit `-permissive` bypass. The
source-admission report is `build/validation/global-grant-ec41-fresh.json`.
This authenticates the compiler product and its grant behavior; it does not
qualify the proof pair or a Studio app.

The earlier clean `19294e83` Stage1/runtime pair remains historical. Its 103-case
authority smoke and Stage0/Stage1 permission parity still document that snapshot;
its SDK package SHA256 is
`d9664cd42f15cfafadd13bfbe5d110c3c24a6070860b18e27b25d3f4edda1d6e`. Do not
use that older product to claim validation of `ec41ca95`.

The isolated `codex/compiler-global-grant-adoption` branch is based on
`ec41ca95` and has localized grants across 93 compiler source files plus a
prefiltered permission-parameter lookup for generic effect rows. Its exact Stage1
seed now passes: product SHA-256
`a1fc208ff1b68a6025b7d2360294bb26d1d944de092dc38152ac5fedc9a1a5a6`, source-tree
SHA-256 `bd8d8d67cb612f4a335a1d7438a0e4fa9f8a6966d85e576383cf0e2a33029883`, and
matching runtime SHA-256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`. The seed
first exposed 29 locals declared in narrow `can` blocks but used outside; these
are now declared as outer mutable values and assigned inside the grant scopes.
On this exact product the authority reporter passes all 103 cases, the actual
CLI gate passes all 24 controls including the explicit `-permissive` bypass,
and Stage0/Stage1 inferred-row parity passes with Stage0 oracle binary SHA-256
`dd0974e2badb757f585a70956838155d6a7987e6c88d22631a956beba62402cd`. The
Stage1 freshness assertion also passes. The prior product `358a2d3b…` predates
the latest scope corrections and remains iteration evidence only.

The first full strict source check after grant adoption ran for more than four
hours against that pre-scope product and was stopped with exit 143. A
five-second process sample placed 2,915 of 3,203 main-thread samples in
`ga_generic_call_rows`, whose permission-parameter filter rescanned all
annotations for each generic effect row. The source now indexes permission
parameter names once and reuses the indexed predicate. The exact Stage1 product
(`a1fc208f…`, with the matching runtime and source-tree hashes recorded above)
passes `bin/elisac-stage1 -emit check src/driver/elisac.elisa` with zero
diagnostics in 160.90 seconds. `/usr/bin/time -l` reports 914,341,888-byte max
RSS and 1,615,072,784-byte peak footprint; the raw log is
`/private/tmp/global-grant-adoption-a8976-strict.log`. Its reporter, actual
CLI controls, Stage0/Stage1 inferred-row parity and freshness checks all pass.
The earlier `19294e83` inventory found
1,225 missing `Global.Read/Write` grants across 87 files in 171.94 seconds;
that is the pre-migration baseline, not the current inventory. The separate
71-row Stage0 log does not establish Stage0/Stage1 parity.

Compiler source commit `a8976bf3b6f54303fdcdb90e37e8f390ac425dcb` carries this
93-file grant adoption and scan optimization. The proof integration baseline
contains 1,166 proof-source diagnostics across 106 files: 1,128 effectful calls
and 38 direct global accesses. Expression-local grants on calls and narrow
scopes around direct writes are applied in the proof worktree; `git diff --check`
passes, but the exact-pair proof build and replay gates are still pending. An
exploratory unpinned checker also emitted 1,271 compiler-source diagnostics; that
set is excluded because the invocation did not use the proof build's pinned
dependency snapshot. After the active Studio build releases the compiler lane,
build and authenticate the matching proof pair, then resolve only backend
declines reproduced on the newest Studio tuple. `-permissive` remains limited to
explicit bypasses; do not use it as evidence for default grant behavior.

## Earlier qualification follow-up

The refreshed `12120f6b` Stage1/runtime includes upstream `53ae9363` and the
nested match-arm repair. Strict compiler checking, authority and actual-CLI
controls, Stage0/Stage1 parity, Stage1 freshness, gen3 self-host and the focused
aggregate regression pass. The full engine gate has 216 source entrypoints; all
pass `-emit check` and the uncached compile-and-run suite passes 216/216. The
proof integration's earlier 1,166 diagnostics predate the corrected `trusted`
behavior; recapture that inventory and qualify the generated proof pair against
the latest clean compiler/runtime before accepting the migration.

At that checkpoint the compiler source had advanced to `4655dbaa`. Its installed, provenance-checked
Stage1/runtime passes compiler Global authority controls and the Character
Course strict project-context checks after engine-side grant adoption. The full
engine gate on that exact product is recorded below. Proof-pair freshness,
course native build/package, and Studio acceptance remain open. See [the exact
Character Course grant record](global-grants-character-course-4655dbaa.md).

<a id="current-4655dbaa-engine-gate"></a>
## 4655dbaa engine gate — 2026-10-09 (previous installed tuple)

Engine commit `565b39c1` contains the Character Course scopes and explicit tail
returns required by default Global grant checking. The installed Stage1 product
`/Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1` has compiler source
revision `4655dbaa17131bcb158be69d5b4939bf64eb6bea`, source-tree SHA256
`40b306621d1e1aa7cdd68a73179360ba1b6b9ba61aae2d491cd3087d4b819fff`, Stage1
SHA256 `cfcdc1fa99752f417c4267f125ed3419be26e569115fb28df3027095cb7e1da2`, and
matching runtime SHA256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
Installed provenance passed; the compiler and runtime hashes were checked before
the run and remained unchanged afterward.

All five Character Course public entrypoints pass strict `-emit check` with zero
diagnostics and without `-permissive`; detailed wrappers and logs are in
[the Character Course evidence](global-grants-character-course-4655dbaa.md).
The full gate command
`python3 scripts/run_tests.py /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 -j 2 --no-cache`
compiled and ran 216 tests: 216 passed, 0 cached, 0 remote, exit 0 in 36
seconds. This closed the engine manifest gate on the 4655dbaa tuple at that checkpoint.
It does not qualify every source outside the manifest, the proof pair, a fresh
Character Course native build/package, or the current Studio app.

The C-ABI Maze example, which is outside the 216-entrypoint manifest, was
separately requalified on this exact compiler/runtime tuple with
`ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 python3 scripts/embed_probe.py`.
The command exited 0: it emitted and linked the C archive, exercised session
and gameplay exports, mapped a live SDL key event into gameplay, and passed the
world/map and repeated-call checks. The Stage1 and runtime hashes above were
rechecked after the run and were unchanged. This closes the Maze C-ABI gate only;
the proof pair and fresh Course/Studio native builds remain open.

## Current b11e9121 engine gate — 2026-10-09

The installed compiler at `/Users/torarinvikbjarko/.elisac/stage1` identifies
source revision `b11e9121c64d94bbc8881db58ae850bf4933ceb9`, source-tree SHA256
`40b306621d1e1aa7cdd68a73179360ba1b6b9ba61aae2d491cd3087d4b819fff`, Stage1
SHA256 `1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24`, and
runtime SHA256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
Installed provenance passed before and after engine qualification; the product
and runtime hashes remained unchanged. The installed artifact identity is tied
to the exact source-tree digest above, independently of later worktree changes.

On this exact product, `ELISA_STAGE1_BIN=/Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 bash test/parity/mutable_global_cli_smoke.sh`
passes. It confirms ungranted reads and writes fail by default, read-modify-write
requires both grants, exact local grants pass, and `-permissive` bypasses each
missing-grant case. The uncached engine gate
`python3 scripts/run_tests.py /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 -j 2 --no-cache`
passes 216/216 in 27 seconds (0 cached, 0 remote). All five Character Course
entrypoint wrappers pass strict `-emit check` with zero diagnostics and no
`-permissive`; per-entry logs are in
`build/validation/character-course-*-b11e-clean-check.log`. The separate C-ABI Maze
probe also exits 0 after archive generation, native linking, session/gameplay
calls, live SDL input and the 100,000-call bridge check.

These results qualify the direct CLI behavior and listed engine consumers on
the b11e9121 product. The compiler's 28-case inferred-row propagation helper
is still unresolved: it expects `S <severity>` records that the current
`parse_report` source does not emit, so no Stage0/Stage1 propagation-parity
claim is made for this product. The proof pair, fresh Course executable/package
and Studio app remain open.

The engine test runner invokes `scripts/qualify_global_grants.py` before
compiling its suite, pins the check to the selected compiler's SHA256, and
writes a product-specific report under
`build/validation/global-grant-cli-qualification-<sha12>.json`. This prevents a
replacement compiler from silently weakening the default policy while still
allowing the explicit `-permissive` migration path. On 2026-10-09, the runner
entrypoint was exercised with a temporary empty test manifest on installed
Stage1 `1505c598…`; the preflight passed 24/24 and the runner exited 0. Its
report is `build/validation/global-grant-cli-qualification-1505c598a71e.json`.
The integration unit controls pass 10/10, including a check that runner
execution stops before engine work when the grant preflight fails. The uncached
216-test result above was qualified separately on the same product before the runner change. The normal
runner was then rerun on 2026-10-09 with `-j 2`: its grant preflight again
passed 24/24 and all 216 engine tests passed with 216 compile results cached,
zero remote compiles and exit 0 in 13 seconds. The exact command was
`python3 scripts/run_tests.py /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1 -j 2`.

On the same identity-pinned Stage1, the engine preflight checks global-mutable
array and aggregate access. Indexed reads require `Global.Read`, indexed stores
require `Global.Write`, a store whose index reads another global requires both,
and aggregate-field reads/writes/read-modify-write enforce the matching grants.
The expanded 21-case source set (42 invocations including `-permissive`) passes
42/42 controls through the normal `scripts/run_tests.py` entrypoint using a
temporary empty manifest. The compiler binary hash remained
`1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24`; the
product-pinned report is
`build/validation/global-grant-cli-qualification-1505c598a71e.json`. The
integration unit controls pass 20/20, including strict entrypoint discovery,
product and recursive include pinning, and rejection of unrelated syntax errors
that mention the permission name. The refusal matcher accepts only a Global permission
diagnostic or a diagnostic identifying a mutable global. The 89-entry strict
runtime-wrapped census is now an automatic runner preflight; its implementation
and post-migration 216-test result are recorded in [the entrypoint census
note](global-grant-entrypoint-census-1505c598.md). The proof and fresh consumer
build gates remain open.
