# ActionInput storage-bounded scans

Date: 2026-10-07. Compiler: installed `75568f889c2e556afaa8e4810bedf5c57f523245`.
Prover generation: `87a538351562444ebe0fdb2adace5a53` (`fc21d99d`).

## Change

All seven binding scans iterate within `Limits::MAX_BINDINGS` and stop at the live
prefix indicated by `binding_count`. For a valid count this visits the same rows
in the same order. An oversized private count cannot extend a scan past storage.
`binding_at` and `rebind_checked` also reject indices beyond storage independently
of the live count. `device_index` now declares its three-device bound explicitly.
Existing error precedence, constructors, captures and public signatures remain.

This removes dependence on an unproved record-wide capacity invariant for these
scans. The independent `binding_count` invariant and general sibling-field stability
in captured loops remain prover work; neither is assumed as a trusted fact.

## Runtime evidence

`test/action_input.elisa` now exercises a full 64-row table: the last row, index 64
refusal, last-row rebinding, first/last key events, focus-style clearing, context
switches and disconnection cleanup. All six existing ActionInput clients pass with
uncached compilation (`build/validation/action-input-bounded-runtime.log`).

`test/action_input_capacity_bounds.elisa` uses a test-only module extension to set
an oversized private count after one valid binding. Lookup/rebinding past capacity
are refused; event delivery, held-state clearing, context/disconnect cleanup and
both duplicate and capacity rejection complete without an out-of-bounds access.
The same probe against the previous implementation traps (exit -5) at the invalid
lookup. The current fixture passes strict compilation and runtime at O0 and O2:

```
DEVELOPER_DIR=/Library/Developer/CommandLineTools ~/.elisac/elisac-stage1 -emit exe -O0 -o build/validation/action-input-capacity-o0 test/action_input_capacity_bounds.elisa
build/validation/action-input-capacity-o0
DEVELOPER_DIR=/Library/Developer/CommandLineTools ~/.elisac/elisac-stage1 -emit exe -O2 -o build/validation/action-input-capacity-o2 test/action_input_capacity_bounds.elisa
build/validation/action-input-capacity-o2
```

The first full uncached run compiled all 215 tests; only the new fixture failed an
incorrect expectation that capacity rejection precedes duplicate rejection.
After correcting that assertion, all **215/215** run successfully (214 compilation
cache hits, one rebuilt fixture). See `build/validation/compiler-755-bounded-storage-tests.log`
and `compiler-755-bounded-storage-tests-final.log`. Audio was forced unavailable.
Source-length and module-hygiene checks pass; no physical-device claim follows.

## Proof evidence and remaining work

`proof/action_input_context.elisa` includes the real implementation. Its complete
report now has **221 obligations, 174 replayed certificates, zero replay gaps**.
`device_index`, `binding_at` and `set_context` are verified in the declaration report.
The prior report had 216 obligations and 129 certificates; inventories differ.
Opaque float/math resource calls, call-state invalidation, late rebind/apply bounds
and some control-flow budget limits remain. The module is not fully proved.

The full 73-row uncached engine sweep remains **69/73**, with the same failures:
ActionInput context/deadzone, AudioAnimEvents and SoundAssets. Reports:
`build/validation/input-bounded-storage.json` and `proof-bounded-storage-sweep.log`.
Full shared/native gates and broader toolchain matrix qualification remain open.

## Float ownership classification follow-up

Prover `b5ff462f` adds `f32`/`f64` to the ownership scalar classifier; this does not
add integer arithmetic witnesses or IEEE numerical laws. It also recognizes copies
into an extern whose unique source declaration has plain scalar parameters and a
plain scalar/void result. Parameter ranges, all retained pointer/provenance flags,
return qualifiers, local callee shadowing and declaration collisions are checked.
Effect rows and native result facts remain separate checks; no body, purity or
no-retention guarantee is inferred. References and moves retain resource handling.

Strict O2 pair `48df3f7c3d2547479426c7604e056b45` uses compiler `75568f88` and
unchanged replay code. Eleven controls pass both whole-file and function routes:
f32/f64 and mutable scalar values, float fields/elements and an integer field are
accepted; explicit field borrows, overlapping mutable float borrows, unknown f32/f64
reference calls and NaN reflexivity are refused. Extern effect containment controls,
IEEE literal forgeries, mixed float/integer bounds, scalar-field priority, record
fixed arrays, qualified block ranges and the kernel inventory also pass.

ActionInput now has **217 obligations, 177 replayed certificates, zero replay gaps**,
and 43 findings. The false `fabsf` resource refusals are gone; the `bind` wrapper
still has an opaque resource call. Inventory changes reflect removal of invalid
resource obligations, not removal of source checks. The uncached sweep remains
**69/73** with the same four failures; the module is not fully proved.
Logs: `build/validation/proof-float-resource-build-3.log`,
`input-scalar-extern-resource-final.json`, and
`proof-scalar-extern-resource-final-sweep.log`. Full prover matrix qualification
and shared/native gate completion remain open.

## Fixed-array index admission and scoped device slots

The fixed-place collector already establishes built-in storage identity. The prover
now admits that identity independently of the separate bounded scalar-witness walk,
so an array late in a record remains indexable without increasing either budget.
Seven controls pass both whole-file and function routes: early/late fields and a
qualified extent are accepted; wide bounds, missing guards, empty storage and
writes through shared references are refused. No trusted assumptions are added.

ActionInput binds device indices in a named region for writes and a value block
for reads. These retain the style guide's temporary lifetimes and expose stable
resource places. The strict O2 prover pair is `99e7fd758e344a5eae7cf64025a44ebd`
with compiler `75568f88`. The actual source report has **228 obligations, 198
replayed certificates, zero replay gaps**, and 34 findings. It is not fully proved.

Runtime gate: **215/215**, with 208 cached compiles and seven rebuilt tests.
The uncached proof sweep remains **69/73**, failing ActionInput context/deadzone,
audio animation events and sound event assets. Logs:
`build/validation/input-scoped-slots-runtime.log`,
`input-scoped-slots-proof-sweep.log`, and `input-scoped-slots.json`.
Full prover compatibility and shared/native qualification remain open.
