# Public example default-grant adoption — 2026-10-09

The default-enforcement compiler exposed missing effect contracts in the
environmental-effects and physics-interactables examples. Their stateful
entrypoints now declare `Global.Read` / `Global.Write` and scope the accesses
inside local `can` blocks. This includes both physics entrypoints and its hidden
self-test entrypoint. The change is limited to effect declarations and explicit
returns needed to preserve the function results; gameplay policy is unchanged.
The existing minimal-application, Character Course and Maze consumers retain
their strict grant contracts.

The affected project entrypoints pass strict `-emit check` on saved compiler
Stage1 SHA256
`5888942e08da166185c76a8e6a13d774c3aa57cf290f093f1c9ce03dd60a887e` and
runtime SHA256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`, without
`-permissive`. The environmental-effects native SDL3/Metal smoke builds and
runs; its impact assertion detects 2,580 changed pixels out of 2,304,000 with
mean RGB delta 0.00058. The physics-interactables hidden native self-test
builds and exits 0.

The semantic wrappers include `src/runtime/public.elisa` and each project
entrypoint. The compiler was invoked as
`$ELISA_COMPILER_BIN -emit check <wrapper>` with `ELISA_RUNTIME_OBJ` set to the
matching runtime object. Per-entry logs and wrappers are retained in
`build/validation/global-grants-public-examples-20261009/`.

Native acceptance commands:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/build/runtime/elisacore_runtime.o" \
/opt/homebrew/bin/python3 scripts/environmental_effects_smoke.py

DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/build/runtime/elisacore_runtime.o" \
/opt/homebrew/bin/python3 scripts/elisa_build_run.py run \
  --project examples/physics_interactables \
  --main self_test_main.elisa \
  --output build/physics-interactables-global-grants-20261009
```

Build provenance is retained at
`examples/environmental_effects/build/elisa-environmental-effects.provenance.json`
and `examples/physics_interactables/build/physics-interactables-global-grants-20261009.provenance.json`.
Their binary SHA256 values are
`3755fa11637078099aa7566ec62d22a2000484316755029ab455be690e1c456f` and
`9b9bdb6a8599dc6e425660e9cad998a8a7e33b7511b5b41814d714c8f1c5d3fd`,
respectively. Both builds used the saved `b11e9121` Stage1 product above.

This closes only the default-grant integration for these example entrypoints.
It does not close proof-pair integration, full compiler propagation parity,
the fresh Character Course package, Studio consumer, hosted CI or the complete
engine-wide strict consumer census. No new pure policy was introduced, so this
slice adds no implementation-linked proof obligations; the compiler's existing
grant regressions remain the policy proof controls.
