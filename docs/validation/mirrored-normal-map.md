# Mirrored-UV normal-map render reference

**Date:** 2026-09-24
**Scope:** Verify the complete glTF cook, snapshot upload, Wicked PBR shading,
and rendered image for a tangent-space normal across a mirrored UV seam.

## Fixture and assertions

`test/fixtures/mirrored_normal_panel.gltf` has two coplanar quads sharing an
edge. The right quad reverses U. Both use the embedded tangent-space normal
`(1, 0, 0)` with `normalTexture.scale=0.75` and a separate dark occlusion map
with `occlusionTexture.strength=0.65`. MikkTSpace splits the two shared seam
vertices, producing eight cooked vertices from six inputs and opposite
tangent-frame handedness for the two charts.

`test/render_scene_mirrored_normal_native.elisa` registers the cooked mesh,
its separate normal and occlusion textures, and its authored material through
the production snapshot asset path. It isolates the rendered instance, sets a
fixed camera and directional light with ambient fill, checks that Wicked
received two triangle-consistent handedness groups with opposite signs,
verifies the authored normal scale and AO strength in Wicked's component and
packed shader material, and reads the left and right rendered patches. Both
patches must be readable and differ in luminance by at least 0.025. It saves a
frame at strength 0.65, sets the material strength to zero, advances four
frames, and saves a control frame.

The normal-map image check samples the same normalized regions and requires at
least 0.04 luminance contrast. The AO check compares those regions across the
two captures and requires at least 0.01 average darkening when strength 0.65
is enabled. The latest Metal run measured normal-map contrast 0.1041 and AO
darkening 0.0503. The normal contrast's sign/order is intentionally not
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
and material probes, LOD image comparison, mirrored-normal comparison, and
the AO-strength versus zero-strength capture. The captures are generated
build artifacts rather than byte-exact goldens; the checks use stable spatial
contrast and luminance-change conditions.

## Limits

This verifies the current Metal backend. Other graphics backends remain
unverified. Wicked stores normal-map strength in a finite half-float field, so
the cooker accepts signed scales within ±65504 and rejects values it cannot
represent. Broader companion-map coverage remains open under R04.
