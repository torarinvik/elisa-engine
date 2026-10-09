# Global grant compiler-candidate history

This file preserves rejected candidates, CLI qualification design, provenance-drift controls, and pre-adoption Studio evidence. The current policy and active integration evidence are in [global-mutable-grants.md](global-mutable-grants.md).

## Previously validated compiler candidate — cfbb8a8b

The default-grant adoption branch includes upstream update `53ae9363` as merge
`cfbb8a8b19ccebcdbf437875283c316dd1d2ea98`. The exact merged Stage1 and matching
runtime are authenticated against that source:

- Stage1 SHA-256: `95d2f62660acb57fdb4d5f07623f5c6483487885cad9511defb0a4f7836350fd`.
- Compiler source tree SHA-256: `9934c633f646b9dbaf73edac7dd0fc0d7391808ea70c3cb6988556ce197db5d5`.
- Runtime object SHA-256: `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
- Strict compiler source check: pass in 90.01 seconds; max RSS
  1,633,943,552 bytes. Log: `/private/tmp/global-grant-adoption-53ae-strict.log`.
- Actual CLI default-grant controls: 24/24 pass, including the explicit
  `-permissive` bypass. Report: `/private/tmp/global-grant-adoption-53ae-cli.json`.
- Mutable-global authority reporter: 103 cases pass.
- Stage0/Stage1 permission-row parity, Stage1 freshness and gen3 self-host
  fixpoint pass.

This compiler result does not yet qualify the proof or Studio products. The
proof worktree's earlier 1,166 finding inventory predates the corrected
`trusted` semantics and must be recaptured. During integration, both
`Semantic::check_full_into` call sites were updated to grant `Global.Read` and
`Global.Write` and to pass the current final `carrier_surface=false` parameter;
the exact pair rebuild and replay acceptance remain pending. This Stage1 product
also predates the match-arm backend fix recorded below.

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

The main-based integrated product must pass the full 24-control actual-CLI gate
before promotion. For an exact candidate, run:

```sh
python3.14 scripts/qualify_global_grants.py --compiler /absolute/candidate/compiler --report build/validation/global-grant-cli-qualification.json
```

The 24 controls use no opt-in headers. They cover missing read/write grants,
read-modify-write with either/both grants, local grants, local shadowing,
transitive callers, the rule that a function signature does not grant its own
body, and the explicit permissive bypass. Required refusals must
exit 1 with the named permission diagnostic; unrelated errors and silent
acceptance fail qualification. Positive cases and every permissive case must
succeed. The report records source hashes, outcomes and diagnostics; it is source
admission evidence, not product/runtime authentication or full promotion.

Candidate `fb8e0927`'s historical 22-control report passes 17/22 and fails all
five required default refusals
(`build/validation/candidate-global-grant-full-cli-controls.json`). That report
predates the corrected gate: four positive fixtures used function-level effect
signatures as if they were local body grants. Keep the five refusals as
historical rejection evidence, but do not compare its aggregate score with the
current 24 controls.
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
parity suite. The later clean `ec41ca95` compiler/runtime tuple passes the 24-control
actual-CLI grant gate, and the consumer owner reports strict semantic preflight passes.
O2 Studio object compilation exceeded the 4 GiB and 6 GiB guards. One earlier
8 GiB retry was stopped after UI dependencies changed. On the later stable
718-input snapshot, another 8 GiB attempt reached 8.04 GiB after 65 minutes and
was stopped by its memory guard. A 12 GiB retry with compiler `ec41ca95` was
stopped after 21 minutes when free swap reached 360 MiB. It produced no object
or compiler diagnostic; it is not a current-app acceptance result. The build
lane is released, but swap remains nearly full. After memory recovers, rebuild
and qualify the merged default-grant compiler, then build the sealed consumer
with its matching proof frontend.
No current bundle, playback, redraw or reveal acceptance is established.
No full prover inventory, renderer lifecycle or native release gate is established by
these controls.

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
