# Physics character validation

`PhysicsRuntime::CharacterDesc` configures the capsule, slope limit, gravity,
collision category, and maximum step height. A zero `step_height` disables the
step assist; positive values allow the Jolt adapter to assist over a short
vertical obstacle up to that height. The native boundary rejects values above
2 physics units. The assist checks the character's collision group, vertical
obstacle face, headroom, and the top surface before applying upward velocity.
The ordinary Jolt character collision solver remains responsible for contact
and landing.

`test/physics_character_probe.elisa` covers standing and crouched capsule
clearance, grounding, jumping, ramp and bounded step ascent, moving-platform
velocity, collision categories, and fixed-tick cadence. Its step fixture walks
the character over a 20 cm box with a 35 cm configured step bound, then checks
that the character has advanced across the obstacle, gained height, and landed
grounded. `walk_to_x` carries only its completion flag across scoped loop
iterations; the per-tick movement values stay local to each iteration.

`test/physics_character_corners_probe.elisa` drives the character diagonally
into a right-angle wall end. It checks the character stays on the blocked side
until its capsule clears the end, slides along the wall, then moves around the
corner and remains grounded. Only the two cross-iteration validation flags are
carried by the scoped loop; each requested move, fixed tick, and sampled pose
stays local to the iteration.

Run the integrated Jolt/Wicked case on macOS with:

```sh
ELISA_COMPILER_BIN="../Elisa-compiler/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="../Elisa-compiler/build/runtime/elisacore_runtime.o" \
PYTHON_BIN="/opt/homebrew/bin/python3.14" \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_NATIVE_SMOKE_ONLY=world-physics-pose-smoke \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py
```

Validation on 2026-09-26:

- The focused SDL3/Metal smoke passed after rebuilding
  `WickedEngine_common` and `WickedEngine_ext_shaders` against the current
  Wicked/Jolt archive. The `world-physics-pose-smoke` ran the shared
  `PhysicsCharacterProbe` through `WorldPhysics` and completed both bounded step
  ascent and corner sliding fixtures.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py`, and
  `git diff --check` passed.

Corner sliding and an authored interactive obstacle course remain open under
P05.
