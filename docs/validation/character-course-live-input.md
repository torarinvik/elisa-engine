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
drives W, C, Space, P, R and Right-arrow events through SDL's event pump and
normal action bindings. It checks pause and resume, traverses the step, crouches
through the tunnel and stands, jumps onto the ramp/platform route, turns into the
goal lane and wins. It then restarts through the live R binding and checks the
character returned to its spawn. Finally, it walks backward off the course and
checks the live failure state. The pilot checks each milestone and fails on a
timeout or unexpected course failure. Streamed floor cells stay valid throughout.

```sh
ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" \
ELISA_RUNTIME_OBJ="../Elisa-compiler/build/runtime/elisacore_runtime.o" \
WICKED_ROOT="../elisa-boxing-wickedengine" \
WICKED_BUILD="../elisa-boxing-wickedengine/build-elisa-sdl3" \
ELISA_SDL3_LIB_DIR=/opt/homebrew/lib \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
PYTHONPATH=scripts \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py \
  --only character-course-live-input-smoke
```

Fresh reruns on Stage1 provenance `bc8def2e` (123.563 seconds) and `54178854`
(265.037 seconds) passed, including compilation and linking. The latest run set
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1` in the parent environment, keeping the
entire hidden app run on the silent route. Results are recorded in
`build/native-smoke/character-course-live-input-smoke.json`.

The live-input pilot now leaves each terminal state on screen for one full
frame, then saves the presented win and fall images before restarting or
exiting. The smoke runner decodes both RGBA PNGs, checks matching dimensions
and visible content, and requires the HUD region to differ between outcomes.
The saved images were inspected to confirm the summit and fall messages are
readable at the hidden window's capture size. Artifacts are retained at
`build/validation/character-course-presentation/win.png` and
`build/validation/character-course-presentation/fall.png`. The app process is
launched with `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`.

## Saved-output relaunch (2026-10-05)

The paired `character-course-smoke,character-course-relaunch-smoke` run keeps
the self-test on Elisa's silent fallback, then clears the forced-unavailable
flag only for the relaunch process so it can reopen the saved output by name.
The relaunch mode checks saved progress and device settings; it does not start
`CourseSounds` or play clips or music. Wicked's FAudio backend stays on SDL's
dummy audio driver. The first process logged `voices=0 streams=0`, and both
processes exited with status 0 on Stage1 provenance `e30ca421`.

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=/private/tmp/elisa-submit-block-shareability/scripts/elisac_stage1.sh \
ELISA_RUNTIME_OBJ=/private/tmp/elisa-submit-block-shareability/build/runtime/elisacore_runtime.o \
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py \
  --only character-course-smoke,character-course-relaunch-smoke
```

The runner retains per-process status and logs at
`build/native-smoke/character-course-smoke.json` and
`build/native-smoke/character-course-relaunch-smoke.json`.

Rechecked on 2026-10-05 with compiler Stage1
`541788548651d43dd466d0b5210955eb966eb18e`: both processes passed (223.255s
and 127.806s). The parent environment used
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=0`; the runner forces it to `1` for the
sound-producing self-test and clears it only for relaunch. The self-test's
stress trace reported `voices=0 streams=0`. Relaunch reopens the saved miniaudio
output but never creates course clips or calls playback; Wicked's independent
FAudio instance stays on SDL's dummy driver. Thus this pair checks saved-device
reopening without playing course audio. The first process report is at
`build/native-smoke/character-course-smoke.json`, and the relaunch report is at
`build/native-smoke/character-course-relaunch-smoke.json`.

```sh
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=0 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
ELISA_RUNTIME_OBJ=../Elisa-compiler/build/runtime/elisacore_runtime.o \
WICKED_ROOT=../elisa-boxing-wickedengine \
WICKED_BUILD=../elisa-boxing-wickedengine/build-elisa-sdl3 \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py \
  --only character-course-smoke,character-course-relaunch-smoke
```

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

Manual visible keyboard traversal remains unverified; the hidden route and its
captured win/fall presentations are covered above.

## Visible controls review (2026-10-07; Q07a remains open)

The October 3 packaged app was stale: its `main.elisa` hash matched the current
entry, but its manifest had no include-closure hash. Pressing R in that binary
ended the process with status 3, so that behavior is not attributed to current
source. A new build from clean engine commit `8ef3d4c2` produced binary SHA-256
`e679de6d9f400f5b2c0862ab5baa7a2045707d281685e85854eb41ba97e0d0fa` using the
Stage1 compiler script SHA-256
`2f28e5c9ab40101fbc99c7a0b67f5d86cbba0b1fadf735384767487da60de2b3`. Its
manifest records no tracked or untracked source changes.

For this review, the binary was packaged under `build/` with the 392 compiled
Metal shader binaries from Wicked's prepared shader tree. It launched to the
course scene and presented the keyboard/controller legend. P displayed the
paused status, Tab opened and closed the rebind menu, and R while paused
restarted the character at the entrance and resumed play. The fresh app stayed
open after restart and exited with status 0 when its window was closed. This
replaces the stale-binary observation; it does not complete a human traversal.

The available computer-use key API emits discrete key presses and has no
key-down/hold operation. Repeated W/Up taps did not produce an observable
continuous walk, so visible traversal, held crouch, and manual summit/fall
presentation remain unverified. The hidden SDL-input route and its captured
win/fall images above still pass. The concrete prerequisite for closing Q07a is
a hold-capable interactive keyboard session (or a user-observed manual run).

Build and stage commands, run from the engine root:

```sh
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
ELISA_RUNTIME_OBJ=../Elisa-compiler/build/runtime/elisacore_runtime.o \
WICKED_ROOT=../elisa-boxing-wickedengine \
WICKED_BUILD=../elisa-boxing-wickedengine/build-elisa-sdl3 \
ELISA_SDL3_LIB_DIR=/opt/homebrew/lib \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
/opt/homebrew/bin/python3.14 scripts/elisa_build_run.py build \
  --project examples/character_course \
  --output /private/tmp/character-course-q07a-manual-2026-10-07

python3 scripts/package_macos_app.py \
  --project examples/character_course \
  --executable /private/tmp/character-course-q07a-manual-2026-10-07 \
  --output build/CharacterCourse-Q07a-2026-10-07.app \
  --name 'Character Course Q07a' \
  --bundle-id org.elisa.character-course.q07a-review \
  --shader-root ../elisa-boxing-wickedengine/WickedEngine/shaders \
  --compiled-shaders-only
```

The staged app is a local ignored build artifact, not a release package.
