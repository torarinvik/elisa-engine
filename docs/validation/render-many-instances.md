# Many instances and lit materials

`render-many-instances-smoke` (test/render_many_instances_native_main.elisa) fills the render
scene's 4096-slot instance table with boxes on a 64-wide grid and checks the following:

- One more create is refused.
- After two frames, every instance's world position read back from Wicked matches its grid
  point, and every instance is still unlit.
- A shadow-casting directional light is added. Every instance is then raised by 3 units, and
  every odd instance gets PBR roughness and metallic values. After two more frames, every
  instance's position and material read back correctly: odd instances are PBR with their exact
  values, even ones stay unlit.
- After everything is destroyed, the instance, light and scene-entity counts return to the
  baseline.

The readback is the new `RenderScene::probe_instance` (native `elisa_render_scene_v1_instance_probe`).
It converts Wicked's world position back to Elisa axes, where x is flipped.

Two controls each fail with status 48: forcing metallic to zero in `set_lit_material`, and
dropping the y translation in `set_transform`. The smoke checks scene state and does not check
pixels.
