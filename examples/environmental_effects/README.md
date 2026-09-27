# Environmental effects

This small SDL3/Wicked scene demonstrates gameplay-owned, event-driven effects.
The glowing impact point is an Elisa `World` entity. A typed world event selects
an emitter profile and a scorch decal profile from `WorldEffectAssets`; the
`WorldEffects` service attaches both to that entity, ticks the sparks, and
expires the emitter after five scaled seconds. The decal stays until cleanup.

Build and run it from the engine root:

```sh
python3 scripts/elisa_build_run.py run --project examples/environmental_effects
```

To build, render a baseline and impact frame, and verify that the effect changes
the image:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
python3 scripts/environmental_effects_smoke.py
```

The smoke uses the same project and runtime path as the example, but hides the
window, captures before and after event dispatch, and exits after checking
effect cleanup. Screenshot files are written under a temporary directory.
