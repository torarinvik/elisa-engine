# Global mutable identity grants

## Required compiler policy

The compiler must enforce `Global.Read` for reads of `global mutable` values
and `Global.Write` for writes by default. A read-modify-write needs both.
`-permissive` bypasses these grant checks. Default enforcement, selective grants,
qualified names, shadowing, indexed/member writes and transitive calls require
compiler-owned positive and negative regressions. This engine preparation does
not establish that compiler policy; the compiler chat is implementing it.

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

The granting compiler's default rejection behavior is not present in this
frozen product. Once its new product is available, requalify the callers' grants
and the default/permissive accepted/rejected controls. Do not use `-permissive`
for the engine's ordinary qualification gate. Full native compatibility also
remains open because of the intermittent effect-memory failure.

## Candidate CLI boundary — not qualified

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

Before replacement compiler promotion, run:

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
