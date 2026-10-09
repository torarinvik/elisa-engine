# Global-grant candidate and consumer history

This note preserves earlier compiler candidates and engine source-boundary
qualification. The active policy and current product evidence are in
[global-mutable-grants.md](global-mutable-grants.md).

## Previous exactly qualified compiler candidate — 12120f6b

Exact source/product qualification and engine consumer fixes are recorded in
[the 12120f6b candidate evidence](global-grants-12120f6b.md). All 216 engine
gate sources passed semantic checking and all 216 compile-and-run tests passed
on that exact pair. Full proof-pair, latest-source requalification and Studio
acceptance remained open.

## Superseded compiler milestones — cfbb8a8b and 107f5e14

`cfbb8a8b` added upstream update `53ae9363`; `107f5e14` then added source fix
`9d2cf1b65d526c681b9fd8e282d0124569f3eb04`, which preserves a value yielded by
a nested `can` / `trusted` block when it is the final value of a match arm. The
reproducer is `test/repro/nested_enum_permission_block_match_return.elisa`, with
parity control `test/parity/nested_enum_permission_block_match_return_smoke.sh`.
The combined `12120f6b` source includes both changes and the function-value
large-aggregate fix; its exact Stage1/runtime qualification is in the linked
candidate evidence. Older products do not qualify current proof or Studio work.

## Historical Studio callback failure

An O2 compile using consumer compiler `9d2cf1b` stopped in LLVM AArch64
SelectionDAG/DAGCombiner without an object. A sealed O0 diagnostic Studio app
opened the file picker but crashed on the supplied high-block FBX. Its crash
report identified `load_fbx_import_job` copying 9,232 bytes from source pointer
`1` into the inline `Job` value. The Elisa caller passes that aggregate through
a function value; ordinary function calls use indirect arguments and hidden
sret returns for aggregates of at least 1,024 bytes, while the function-value
emitter previously constructed a direct aggregate signature. Compiler fix
`37091274` emitted the indirect argument and sret conventions, rejected
unsupported large-aggregate closures, and added a focused native regression.

The mocap-cleaner owner rebuilt Studio with `37091274` to retry the original
FBX import and playback. O2 reached LLVM AArch64 DAGCombiner at about 28.4 GB
physical memory and was stopped without an object. Mocap-cleaner commit
`0a39989b` added `STUDIO_OPT_LEVEL` while keeping O2 as default. A sealed O0
diagnostic app built against compiler SHA256
`ad0e16c9eddea0132a6ffee252f3dab4a3008dd805c042e1cb01cd48791a78f1` and
runtime SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
The supplied FBX SHA256 was
`50048a8a08f307d378e83d976462addcac62b5529bf60ae690da200d9d9f4485`; the app
quit when that FBX was opened. Crash report
`MocapStudio-2026-10-09-042258.ips` identified a null arena in `arena_alloc`:
`StudioFbxImportWorker.path_copy` receives its hidden region in x1, but the
generic function-value callback did not forward that region. The focused
1,024-byte aggregate regression did not qualify the actual 9,232-byte job path.
Compiler change `d5a9b58a` added callback region metadata and task-owned
result-arena transfer, but it was not included in `12120f6b`; integration,
fresh toolchain and Studio retry remained open at that checkpoint. Optimization
was a separate gate.

## Audited application, input and UserData native boundaries

`src/runtime/application.elisa`, `src/runtime/application_input.elisa` and
`src/runtime/user_data.elisa` give native declarations explicit
`can[Unsafe.RawExtern]` contracts. The reviewed C++ shims own process/runtime
state and exchange values through explicit arguments and buffers; audited
entrypoints do not access Elisa `global mutable` bindings. Each Elisa call site
uses a narrow `trusted Unsafe.RawExtern` block, keeping the implementation
detail inside engine wrappers rather than making public APIs unsafe. The
pointer-replay API also lost inherited uncertainty through its
`ApplicationInput` dependency.

On Stage1
`/private/tmp/Elisa-compiler-prover-method-rehome/bin/elisac-stage1`
(SHA256 `5198034700383a76aa25ca7db2e2e31bdd4fa98753f721b53c80233e24257916`):

- `-emit check src/runtime/user_data.elisa` succeeds.
- `-Wnever-leak=strict -emit check src/runtime/user_data.elisa` succeeds with
  zero findings; temporary effect/status locals are scoped with `region` blocks
  and the staged payload read yields its two outputs from a tuple block.
- `test/user_data_probe.elisa` compiles with no global-grant diagnostics; this
  consumer probe previously reported 106 such diagnostics.
- A `# strict` / `# unsafe` fixture including
  `src/runtime/action_pointer_replay.elisa` compiles to an object, covering the
  application and input native call sites as well as their transitive caller.

This is source-boundary qualification, not a full engine grant audit or native
application/UserData runtime test.

## Engine source preparation

The engine has three global mutable identity counters: world epochs, storage
catalog brands and access-frame identities. Each issuance helper encloses its
reads, exhaustion checks and writes in `can Global.Read, Global.Write`. The
monotonic identity algorithm, maximum bounds and typed errors are unchanged. No
`trusted` block conceals effects. Explicit returns preserve the compiler's
control-flow requirements inside grant blocks.

## Focused qualification on the frozen compiler

On frozen compiler `52d60fcf` with its matching runtime, direct executable
compilation and execution of `test/world.elisa`, `test/world_storage.elisa`, and
`test/world_access_serials.elisa` pass their existing assertions. Command:
`python3.14 build/validation/qualify-world-global-grants.py` under the existing
3 GiB / 360 second watchdog; 1.68 seconds / 111,392 KiB peak RSS. Logs:
`build/validation/world-global-grants-runtime.log` and watchdog JSON.

An uncached sweep using verified clean prover generation
`a0ea428da32c4674aa411bb4d0243540` retains all 73 reports / 4,246 obligations,
all proved, certified and independently replayed, with zero errors, diagnostics,
gaps or trusted assumptions (1.87 seconds / 161,952 KiB, original 3 GiB cap).
Command: `python3.14 build/validation/check_world_grants_engine.py`. Reports and
inventory: `build/validation/world-global-grants-engine-reports/` and
`world-global-grants-engine-inventory.json`.

This frozen compiler predates default enforcement. It does not qualify the
integrated engine/Studio build or full native compatibility. Do not use
`-permissive` for ordinary engine qualification. The intermittent effect-memory
release failure remains open.
