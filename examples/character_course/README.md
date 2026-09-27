# Character course

This playable SDL3/Metal example uses Elisa's public `RuntimeServices`,
`PhysicsRuntime`, `ActionInput`, and `RenderScene` modules. Wicked/Jolt runs the
character controller and the visible course uses the same box transforms as
its collision bodies.

From the engine root, build and launch it with:

```sh
python3 scripts/elisa_build_run.py run \
  --project examples/character_course
```

Use **WASD** or the arrow keys to move, **Space** to jump, hold **C** to crouch,
press **P** to pause/resume, **R** to restart from the entrance, and **Escape**
to quit. Reach the green summit marker to win; falling off the course ends the
attempt until you restart. The route has a bounded step, a low tunnel, a
20-degree ramp, a raised platform, and a right-angle corner.

Press **Q** to save a checkpoint and **L** to load it, including after quitting
and relaunching. A checkpoint stores the phase, position, crouch and attempt
count as explicit codes and whole millimetres (`progress.elisa`) in the
application's user-data directory (`ELISA_USER_DATA_DIR` overrides it). A load
validates the entire record, creates the restored character, and only then
retires the running one. A missing, unreadable or out-of-range record leaves the
current run untouched and says so in the status line.

Build and run the hidden check without waiting for input. It covers state
transitions, step traversal, restart, and save/restart/load, including rejected
records:

```sh
python3 scripts/elisa_build_run.py build \
  --project examples/character_course \
  --main self_test_main.elisa \
  --output build/elisa-character-course-self-test
examples/character_course/build/elisa-character-course-self-test
```
