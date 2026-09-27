# Godot lighting adapter

`backends/godot/lighting_service.gd` is the Godot host-side implementation of
the lighting descriptors used by the engine. It owns its `Light3D` children,
one scene-local `WorldEnvironment`, and the sun light. Directional, point,
spot, and rectangular descriptors create Godot's native light nodes. Updates
validate the complete descriptor before changing the live node. Opaque handles
carry owner, slot, and generation identity; stale and cross-service handles are
rejected.

Environment updates stage a new `Environment` before replacing the active one.
The service maps sky panoramas, background exposure, ambient color, fog,
tonemapping exposure, and the environment's sun color/direction. Sun controls
set shadow enablement, bias, and four cascade distances relative to the camera
far plane.

Godot feature differences are surfaced by `capabilities()` and checked during
updates. The Compatibility renderer cannot cast area-light shadows; nonzero
spot inner-cone requests, per-light shadow-map resolution, and panorama
rotation are unsupported by this adapter and return an explicit status. The
outer spot angle and area dimensions remain native Godot settings. Other
rendering methods may support area-light shadows, as indicated by the active
renderer capability.

The headless contract probe covers all four light node types, direction and
cone conversion, area dimensions, capacity, handle ownership and generation,
invalid-update atomicity, environment replacement, sky/fog/exposure, sun
shadow controls, cascade conversion, and unsupported-feature rejection. It is
part of `scripts/check.elisascript` and can also be run by itself:

```sh
godot --headless --path backends/godot --script res://lighting_probe.gd
```

The optional display-backed smoke renders unlit, lit, and moved-point-light
frames and requires visible pixel changes. It writes its captures under the
ignored `build/` directory:

```sh
GODOT_BIN=godot python3 scripts/godot_lighting_visual_smoke.py
```

Verified with Godot 4.7.2 on macOS 27.0 / Apple M5 through Godot's OpenGL
Compatibility renderer on Metal. The 320×200 captures changed 20,857 pixels
from unlit to lit and 20,823 pixels when moving the point light. This is a
rendered-difference smoke, not a tracked reference-image comparison. The
stable lighting references still cover the SDL3/Metal Wicked path.
