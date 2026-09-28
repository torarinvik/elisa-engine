# Typed catch arms

The stage1 backend miscompiles a catch arm written with the `error` prefix,
`error Module::ErrorType.Variant: value`: every error reaches the first typed
arm. The bare form `Module::ErrorType.Variant: value` matches correctly, and an
`error failure:` catch-all is unaffected. The compiler bug belongs to
`../Elisa-compiler` and was not changed here.

## Change

- `World::world_spawn` (`src/world/world.elisa`) maps the allocator's errors
  with bare arms. Before, an exhausted allocator (`MaxId`) reached the
  `InvalidId` arm and raised `IdentityInvalid` instead of `IdentityExhausted`.
- `test/render_scene_effects_native.elisa` moves the event-spawn catch into a
  `spawn_events` helper with bare arms, so each failure keeps its own code
  (120–145) instead of 123. Returning from a typed arm beside `value: value`
  makes the backend decline the caller, so the helper returns an outcome struct.
- `test/world.elisa` adds `identity_error_test`: a world whose allocator is at
  `ENTITY_ID_MAX` must raise `IdentityExhausted` (code 70 otherwise), and one
  with a corrupt cursor must raise `IdentityInvalid` (code 71 otherwise).

No `error X::Y.Z:` arm remains in `src`, `test`, `examples`, `native` or
`proof`.

## Checks and commands

```
../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o $SP/world_test test/world.elisa && $SP/world_test
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

On 2026-09-28 the world test exited 0, `check` exited 0 with `failed: 0`, and
the native gate exited 0.

## Negative control

With `world_spawn`'s arms put back in the `error`-prefixed form, the world test
exited 70. The file was restored, and `cmp` confirmed it matches the verified
copy.

## Gaps

- The backend bug itself is unfixed; the bare form is a workaround.
