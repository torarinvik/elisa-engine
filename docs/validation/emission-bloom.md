# Elisa emission and bloom API validation

`RenderScene::set_emissive` assigns bounded HDR emission to a live scene handle;
the engine switches that material to Wicked's PBR path so emissive radiance is
evaluated. RGB channels accept `[0, 1]` and strength accepts `[0, 64]`.
`RenderScene::set_bloom` controls the scene render path's bloom switch and
threshold; threshold accepts `[0, 64]`.

The native Elisa render smoke sets emission on a rendered instance, enables
bloom with a non-default threshold, and verifies that out-of-range emission and
bloom values return `RenderSceneError.InvalidValue`. It also confirms the scene
renders through Wicked and shuts down cleanly. Run it on macOS with the configured
SDL3/Metal Wicked build:

```sh
python3 scripts/render_scene_native_smoke.py
```

This validates the public API and native render path. A captured electric-fence
reference image is still needed to tune visible halo strength and pulse timing.
