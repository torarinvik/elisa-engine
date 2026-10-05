# Character Course live input and frame clock

Validated on 2026-10-05 with the installed Stage1 compiler
`7b27fa312c5af923f044f6ee0e5e1de4f811f595`, SDL3 and the macOS Metal backend.
The native smoke commands below set `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`,
so Elisa audio uses its silent fallback throughout the runs.

## Live crouch and jump

`character-course-cells-smoke` sends C and Space key-down/up events through the
test-only SDL event hook. The normal application event pump, key bindings,
character controller and physics loop process them. The smoke asserts that the
character crouches, releases crouch and stands, then jumps; it also completes
the existing streamed-cell traversal checks. This is hidden automated input
coverage, not a manual presentation check.

```sh
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py \
  --only character-course-cells-smoke
```

Result: pass (status 0). The run took 190.64 seconds including compilation and
linking.

## Full course through live input

`character-course-live-input-smoke` starts the ordinary hidden course loop and
drives W, C, Space and Right-arrow events through SDL's event pump and normal
action bindings. It completes the step, crouches through the tunnel, releases
crouch and stands, jumps onto the ramp/platform route, turns into the goal lane,
and wins. The pilot checks each milestone and fails on a timeout or course
failure. This run also checks that streamed floor cells stay valid during the
traversal.

```sh
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py \
  --only character-course-live-input-smoke
```

Result: pass (status 0, 110.69 seconds including compilation and linking). The
silent-audio override was enabled for the entire hidden app run.

## Monotonic frame time

The native `FrameInfo.elapsed_nanos` now reports monotonic time since app
initialization. Consumers can subtract consecutive samples for a frame delta.
Previously the native ABI returned only the last frame's duration, although
game code used it as a clock; this prevented timed input releases and fixed
simulation steps from advancing consistently.

The focused `application-frame-time-smoke` pumps twice and checks that frame
count and elapsed time both increase:

```sh
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py \
  --only application-frame-time-smoke
```

Result: pass. The larger `application-native-smoke` still stops during Elisa
compilation at `test/world_physics_pose_probe.elisa:67` because Stage1 rejects a
non-static reference passed to `pool_submit1`; the focused clock smoke avoids
that unrelated physics probe.

Manual traversal and win/fall presentation remain unverified.
