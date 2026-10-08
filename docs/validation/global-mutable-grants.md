# Global mutable identity grants

## Required compiler policy

The compiler must enforce `Global.Read` for reads of `global mutable` values
and `Global.Write` for writes by default. A read-modify-write needs both.
`-permissive` bypasses these grant checks. Default enforcement, selective grants,
qualified names, shadowing, indexed/member writes and transitive calls require
compiler-owned positive and negative regressions. Compiler source
`2a3dce665ff462096de961d340a50cc1ef83369d` and its fresh Stage1/runtime pass the
focused controls described below, including explicit `-permissive` bypasses.
That tuple also produces a sealed Studio app; actual launch and FBX/redraw/reveal
acceptance remain open.

## Audited UserData native boundary

`src/runtime/user_data.elisa` now gives each `elisa_user_data_*` declaration an
explicit `can[Unsafe.RawExtern]` contract. The reviewed implementation in
`native/user_data_abi.cpp` owns its filesystem and service state, receives data
through explicit arguments, and has no Elisa callbacks. It therefore has no
effect on Elisa `global mutable` bindings. The wrapper encloses each raw call in
a narrow `trusted Unsafe.RawExtern` block, preserving the safe public UserData
API and passing strict-unsafe checking.

On Stage1
`/private/tmp/Elisa-compiler-prover-method-rehome/bin/elisac-stage1`
(SHA256 `5198034700383a76aa25ca7db2e2e31bdd4fa98753f721b53c80233e24257916`):

- `-emit check src/runtime/user_data.elisa` succeeds.
- A `# strict` / `# unsafe` fixture including the module compiles to an object.
- `test/user_data_probe.elisa` no longer reports missing global grants in
  `src/runtime/user_data.elisa`; remaining diagnostics are in the unrelated
  application, input and pointer-replay runtime APIs.

This is a source-boundary qualification, not a full engine grant audit or a
native UserData runtime test.

## Engine source preparation

The engine has three global mutable identity counters: world epochs, storage
catalog brands and access-frame identities. Each issuance helper now encloses
its reads, exhaustion checks and writes in `can Global.Read, Global.Write`.
The monotonic identity algorithm, maximum bounds and typed errors are unchanged.
No `trusted` block conceals effects. Explicit returns preserve the existing
compiler's control-flow requirements inside grant blocks.

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

## Historical rejected candidate — fb8e0927

Clean compiler candidate `fb8e0927` Stage1 product
`1e408e0d334997d7d2ea5e722e1bab2fca318c1bca70f0d3147bc8a0ac0acaca`
passes `-emit check` on the three engine identity-counter fixtures. However,
actual default CLI checks also accept ungranted mutable-global reads and writes.
Adding the supported `# globals` header makes the same fixtures reject with the
expected read/write diagnostic. The driver still forwards the source-header probe
as the permission-enforcement switch. This candidate therefore does not satisfy
default enforcement and must not be installed as qualified on this evidence.

Exact results: `build/validation/candidate-global-grants-cli-boundary.json`;
engine source checks: `candidate-global-grants-engine-source-check.json`.
The reproducer was sent to the coordinated compiler owner. No compiler source
was edited and no replacement product installed. The helper regression suite's
explicit ON/OFF success is narrower than actual default CLI enforcement.

## Repeatable actual-CLI admission gate

The main-based integrated product must pass the full 22-control actual-CLI gate
before promotion. For an exact candidate, run:

```sh
python3.14 scripts/qualify_global_grants.py --compiler /absolute/candidate/compiler --report build/validation/global-grant-cli-qualification.json
```

The 22 controls use no opt-in headers. They cover missing read/write grants,
read-modify-write with either/both grants, local grants, local shadowing,
transitive callers and the explicit permissive bypass. Required refusals must
exit 1 with the named permission diagnostic; unrelated errors and silent
acceptance fail qualification. Positive cases and every permissive case must
succeed. The report records source hashes, outcomes and diagnostics; it is source
admission evidence, not product/runtime authentication or full promotion.

Candidate `fb8e0927` passes 17/22 controls and fails all five required default
refusals (`build/validation/candidate-global-grant-full-cli-controls.json`).
Four qualifier unit controls pass, validating expected-policy success, silent
acceptance refusal, unrelated errors and missing executable handling
(`build/validation/global-grant-qualifier-controls.log`). Those unit controls are
registered in the portable native-facing test slot; the actual candidate gate is
an explicit replacement-qualification command so the historical compiler baseline
is not misrepresented as satisfying the new policy.

## Qualified focused compiler candidate — ee028bbb

Compiler source commit `ee028bbba92e5daff9f763c02d81a59c1c98217d`
(`declare runtime wrapper global grants`) is on branch `codex/prover-method-rehome`.
It gives `rt_puts` and `ctx_llvm_codegen_fatal` explicit `Global.Read` and
`Global.Write` requirements, adds positive/negative wrapper controls, and updates
the rehome fixtures to declare their global grants and caller-owned region.

- Clean Stage0 source revision: `778c8281f97c81adbb3bc764b634f263c9f36f52`,
  built at `/private/tmp/elisa-core-stage0-778c8281/compiler/bin/elisac`,
  SHA256 `624fb245cc6b96c71a98d3bff446f877a7fd397e6416e07404e48c9daa909f84`
  (`vcs.modified=false`).
- Stage1 seed command:
  `ELISACORE_BIN=/private/tmp/elisa-core-stage0-778c8281/compiler/bin/elisac ELISA_STAGE1_SEED_MAX_RSS_KB=8388608 PYTHON_BIN=python3.14 bash scripts/elisac_stage1.sh --seed`.
  It completed successfully.
- Stage1 product:
  `/private/tmp/Elisa-compiler-prover-method-rehome/bin/elisac-stage1`,
  SHA256 `e3c3e3808571b1b676c4a4a78fc61e6ccc787cedb17e1465e908955650dbe94f`.
- Provenance:
  `/private/tmp/Elisa-compiler-prover-method-rehome/bin/elisac-stage1.provenance.json`,
  SHA256 `2cef5b8af753b6f3e569c7ed01b77866b2e88035355af3d8549dba00ee3263a2`;
  source tree SHA256 `18e3ee2348f07409d828242b02a88cc8605f67c785f9cc8224da6180420c04ce`;
  build recipe SHA256 `a0a7f5b8753dc01e202828a80b16508facf840aa6ceea7ea2cec0fc597ba9ba5`.
- Matching runtime object SHA256:
  `17a5e88040dbe3f13c0ec70b31c7a0bfab57ad89b5e633760d654260f2dde414`.
- `python3.14 test/parity/mutable_global_grants_smoke.py bin/elisac-stage1`: 52
  grant/global cases and six explicit `-permissive` bypass cases pass.
- `ELISA_STAGE1_BIN="$PWD/bin/elisac-stage1" bash test/parity/protocol_grouped_effects_smoke.sh`:
  all 12 grouped-protocol controls pass, including qualified imported aliases.
- `ELISACORE_BIN="$PWD/bin/elisac-stage1" bash test/parity/runtime_global_grants_smoke.sh`:
  all eight controls pass, including positive exact grants and missing-read/write
  refusals for both runtime wrappers.
- `ELISA_STAGE1_BIN="$PWD/bin/elisac-stage1" bash test/parity/global_rehome_bounded_smoke.sh`:
  the 1,000-overwrite RSS limit passes, and the nested callback/global pose-cache
  reproducer passes at O0 and O2.
- `bash scripts/assert_stage1_fresh.sh bin/elisac-stage1` passes.

The Stage1 source already includes the guarded transitive arena rehome implementation,
exact callee/owner resolution, typed builtin receiver checks and conservative fallback
for unknown targets. This confirms the ownership repair on focused compiler controls,
not in the actual Studio application. The focused suite is not the full compiler
parity suite; the main-based integrated compiler/runtime tuple still needs the
22-control actual-CLI gate, current engine/Studio semantic preflight and real Studio
build. No full prover inventory, renderer lifecycle, packaging or native release gate
is established by these controls.

## Compiler product drift guard

The qualifier now hashes the compiler executable before and after all controls.
For a launcher, supply `--product /absolute/underlying/binary`; pin the authorized
product with `--expected-product-sha256 HASH`. A mismatched hash stops before
compiler invocation, and product mutation invalidates otherwise passing controls.
This endpoint check does not detect a temporary mutation reverted between reads;
use immutable candidate inputs. A launcher's own hash is not its underlying product
identity, so replacement qualification should always name the binary explicitly.

Six qualifier controls pass, including wrong-product preflight and mid-run product
replacement (`build/validation/global-grant-qualifier-product-controls.log`).
The actual candidate path changed from the coordinator's authorized `1e408e...`
to `cf34f1...` while retaining the same source revision in its new sidecar.
The expected-hash gate rejects it without running any semantic control:
`build/validation/global-grant-candidate-product-drift.json`.
The coordinated paired prover build was not started; the frozen tuple was requested.

## Follow-up verifier repair and Studio preflight

Compiler source `2a3dce665ff462096de961d340a50cc1ef83369d` adds a call-coercion
guard: a by-value optional cannot be passed as a required reference. The prior
path emitted a non-niche `{tag, payload}` aggregate where the callee ABI expected
a pointer. It preserves both narrowed optional-reference forwarding and
`T?&` container borrowing. `test/parity/optional_payload_ref_argument_smoke.sh`
verifies that the unguarded aggregate form is rejected before writing LLVM, a
guarded payload binding passes its address, a narrowed optional reference keeps
its payload pointer, and a reference-to-optional call points to the global
container. Accepted forms pass `opt -passes=verify`.

The fresh Stage1 product for that source tree is
`/private/tmp/Elisa-compiler-prover-method-rehome/bin/elisac-stage1`, SHA256
`bcdb4a4103662e77c9ac8c58d0e870584328761246585cdad24ce31b88f10119`. Its
matching runtime object SHA256 is
`17a5e88040dbe3f13c0ec70b31c7a0bfab57ad89b5e633760d654260f2dde414`; clean
Stage0 SHA256 is
`624fb245cc6b96c71a98d3bff446f877a7fd397e6416e07404e48c9daa909f84`; source
tree SHA256 is `bfc703a4fe39a9e480a5e05851fcbaa1fbf92e269111935ef33e2a1e065be4dd`,
and build-recipe SHA256 is
`a0a7f5b8753dc01e202828a80b16508facf840aa6ceea7ea2cec0fc597ba9ba5`.
Freshness assertion, 52 grant cases, six `-permissive` cases, eight runtime
grant controls, 12 grouped-protocol controls, strict unsafe, export-effect alias,
void wrapper, same-name export and optional/reference controls pass on this
product.

Linked engine source commit `4b0a9af7e4745ea0866813cd4ea2396b17de4396`
declares the native effect contracts. The Studio caller now explicitly borrows
the optional recovery ledger and refuses the operation when it is absent. A
previous sealed attempt also needed the generation inventory to include
`semantic.log` and `compiler.log`. The exact compiler/UI/engine tuple then built,
verified, linked and sealed `build/MocapStudio.app`. The consumer owner committed
the report-reference repair and plan update as `bca72d19`; that project base was
opened for the authorized FBX workflow. Bundle metadata records engine
`4b0a9af7`, UI `261363eb`, compiler `2a3dce66`, and marks the project and UI
worktrees dirty; the seal verifies the recorded input snapshot and copied
executable. The consumer navigated to the supplied FBX, but the file picker
timed out during Open. A process sample showed the app responsive in its event
loop, and an immediate follow-up check found no FBX file open. Later, the app
displayed `high block_Unreal5.6.fbx` as a 337-frame take, confirming that the
worker completed with this compiler product. Its skinned surface is available
and `M` switches Character/Skeleton. Playback remains unqualified: the converted
GLB has sparse two-key channels mixed with 337-frame channels, but the Studio
timeline requires aligned sample times. The converter owner is fixing the import
grid; verify visible pose advancement before recording acceptance.
`mocap-cleaner/scripts/check.sh` also reports that its prover frontend/compiler
manifest is stale relative to the current Stage1 and requires the pair to be
rebuilt and qualified. FBX import, worker completion, surface availability and
Character/Skeleton switching pass in the app. Timeline playback/posed redraw and
observer reveal remain unqualified.
Renderer lifecycle, prover, package and native release gates remain open.

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
