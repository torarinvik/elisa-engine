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

`examples/character_course` is a playable project built from the public
`RuntimeServices`, `PhysicsRuntime`, `ActionInput`, and `RenderScene` modules.
Its visible collision course includes the same step, a crouch-height tunnel, a
20-degree ramp, a raised platform, and a wall end to clear. The separate
`self_test_main.elisa` entry creates the complete course in a hidden SDL3 window
and checks the character settles, traverses the step, and gains the expected
height before shutdown.

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
- Both `elisa_build_run.py build --project examples/character_course` and the
  alternate `self_test_main.elisa` entry built against the public runtime. The
  hidden `elisa-character-course-self-test` then passed on SDL3/Metal.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py`, and
  `git diff --check` passed.

P05 character movement and the interactive course are complete. Broader vehicle,
constraint, ragdoll, and navigation gameplay remain tracked under P06–P09 and
N01–N04.

## First-contact jump regression — 2026-10-07

The live cell pilot exposed a grounded jump consumed by residual fall speed.
At first contact, the controller reported OnGround with vertical velocity
-6.231466; requesting speed 5 left velocity -1.231466. Space reached the action
and native movement API, but the character rose only about 0.016 m.

`native/physics_character_abi.inc` now preserves the backend's horizontal
movement and ensures a grounded jump reaches at least support vertical velocity
plus requested speed. Higher upward momentum is preserved. Airborne and
non-jump movement use the existing backend behavior. Non-finite velocity or
launch targets are rejected before correction.

The shared `native/character_jump_policy.h` passes 144 finite cases and the
observed regression; disabling the correction fails the negative control with
status 1. The cell pilot now waits for standing/ground support and reports its
stage, baseline, peak and observed jump action on completion. Its original
0.15 m rise threshold is unchanged. The native cell gate passed in 196.867s:
baseline -0.035638, peak 1.153066. Native WorldPhysics character/contact checks
passed in 60.727s and physics queries in 53.903s. The updated main live-input route also passed in 203.575s. These are native tests, not a whole-program formal proof.

See [scheduled contact delivery](world-physics-contacts.md) for the work that
exposed the failure and the retained pre-fix diagnostic.
