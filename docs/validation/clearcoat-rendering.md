# Clearcoat rendering validation

The render-scene native smoke compares a painted material without clearcoat to
the same material with `KHR_materials_clearcoat`, using cooked and
hand-registered material paths. A controlled point light sits above the panel
and near the view direction so the coat's specular response is visible. The
test samples red and blue patches in the 3D render result and requires at least
0.01 mean-luminance difference for each coated variant from the baseline.

Run on the supported macOS SDL3/Metal target:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py
```

The smoke writes full-frame captures to
`build/render-scene-clearcoat-baseline.png` and
`build/render-scene-clearcoat-coated.png`; light-position captures use
`build/render-scene-point-light-left.png` and
`build/render-scene-point-light-right.png`. The same fixture moves its point
light between two positions and requires at least one painted patch to change
by 0.01 in mean luminance. Captures include the host UI; assertions read the
render path's 3D output directly. This reference confirms the clearcoat
response and live point-light update on Metal; it does not claim cross-device
parity.
