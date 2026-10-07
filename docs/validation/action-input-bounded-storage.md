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
