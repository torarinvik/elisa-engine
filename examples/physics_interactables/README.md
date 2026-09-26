# Physics interactables

This SDL3/Metal scene demonstrates a bounded, motorized hinge door, a vertical
motorized slider lift, and a point-jointed pendulum. The objects are driven by
Elisa's public `RuntimeServices`, `PhysicsRuntime`, `ActionInput`, and
`RenderScene` APIs; the visible transforms are read from the physics bodies.

Build and launch from the engine root:

```sh
python3 scripts/elisa_build_run.py run --project examples/physics_interactables
```

Press **E** to reverse the door motor, **L** to reverse the lift motor, **J** to
push the pendulum, and **Escape** to quit. Run the hidden acceptance check with:

```sh
python3 scripts/elisa_build_run.py build \
  --project examples/physics_interactables \
  --main self_test_main.elisa \
  --output build/elisa-physics-interactables-self-test
examples/physics_interactables/build/elisa-physics-interactables-self-test
```

The acceptance check verifies door and lift travel under their motors, checks
vertical slider motion through the isolated `RuntimeServices` session API, and
confirms that all three authored constraints remain intact after stepping
Jolt. The separate `physics-constraints-smoke` also exercises the rotated
vertical slider through `PhysicsRuntime`, including its limits and motor.
