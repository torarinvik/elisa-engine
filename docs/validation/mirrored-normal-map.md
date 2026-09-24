# Mirrored-UV normal-map render reference

**Date:** 2026-09-24  
**Scope:** Verify the complete glTF cook, snapshot upload, Wicked PBR shading,
and rendered image for a tangent-space normal across a mirrored UV seam.

## Fixture and assertions

`test/fixtures/mirrored_normal_panel.gltf` has two coplanar quads sharing an
edge. The right quad reverses U. Both use the same embedded tangent-space
normal `(1, 0, 0)`. MikkTSpace therefore splits the two shared seam vertices,
producing eight cooked vertices from six inputs and opposite tangent-frame
handedness for the two charts.

`test/render_scene_mirrored_normal_native.elisa` registers the cooked mesh,
its embedded normal texture, and its authored material through the production
snapshot asset path. It isolates the rendered instance, sets a fixed camera
and directional light, checks that Wicked received two triangle-consistent
handedness groups with opposite signs, and reads the left and right rendered
patches. Both patches must be readable and differ in luminance by at least
0.025. The test saves a frame to `build/render-scene-mirrored-normal.png`.

The Python image check samples the same normalized regions and requires at
least 0.04 luminance contrast. The Metal capture on this machine measured
left 0.9117, right 0.6618, contrast 0.2498. The sign/order is intentionally not
hard-coded because graphics backends may reflect tangent-space axes while
preserving the required contrast.

## Validation

```sh
/opt/homebrew/bin/python3 scripts/gltf_mirrored_normal_fixture.py --write-fixture
/opt/homebrew/bin/python3 scripts/cook_gltf_asset.py \
  test/fixtures/mirrored_normal_panel.gltf \
  --asset-path test/fixtures/mirrored_normal_panel.gltf \
  --output build/cooked/subsets/mirrored_normal.elpk
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
/opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py
```

The complete SDL3/Metal render-scene smoke passed, including the native seam
and frame probes, LOD image comparison, and mirrored-normal image comparison.
The capture is a generated build artifact rather than a byte-exact golden;
the checked reference is its stable spatial contrast condition.

## Limits

This verifies the current Metal backend. Other graphics backends remain
unverified. Non-default glTF normal-texture scale and occlusion strength are
still rejected by the cooker and remain R04 material-fidelity work.
