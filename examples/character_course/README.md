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
and **Escape** to quit. The course has a bounded step, a low tunnel, a 20-degree
ramp, and a raised platform with a right-angle corner to clear. The green sphere
marks the finish. Build and run the hidden startup and step-traversal smoke
check without waiting for input:

```sh
python3 scripts/elisa_build_run.py build \
  --project examples/character_course \
  --main self_test_main.elisa \
  --output build/elisa-character-course-self-test
examples/character_course/build/elisa-character-course-self-test
```
