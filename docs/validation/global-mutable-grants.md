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
