# Root-motion validation

`RenderScene::set_animation_root_motion` resolves the selected joint's sampled
translation through its parent scale and rotation before extracting horizontal
travel. It returns that displacement in instance-local Elisa metres. The root
pose retains world-up movement while its planar displacement is removed through
the inverse parent transform, including rigs whose up axis differs from Elisa's.

The native SDL3/Metal fixture uses a 0.01-scale, rotated skeleton ancestor and a
root clip with translation on all three authored axes. Its assertions cover the
first sampled delta, the remaining vertical pose, accumulation, forward and
reverse loop wraps, skipped cycles, direction changes, and disabling extraction.
After disabling extraction, it starts a follow-on fade and verifies that the
displayed root pose remains the fade source with no visible jump.
The native render-scene main invokes this as animation case group 271.

The focused fixture can be regenerated and checked on macOS with:

```sh
/opt/homebrew/bin/python3.14 -c 'import sys; from pathlib import Path; sys.path.insert(0, "scripts"); import gltf_skin_self_test; gltf_skin_self_test.write_root_motion_package(Path("build/cooked/subsets/root-motion-skinned.pkg"))'
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
ELISA_ALLOW_STALE_STAGE1=1 ELISA_RENDER_SCENE_RENDER_ONLY=1 \
/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py
```

The render-only native smoke passed on SDL3/Metal with Wicked 0.72.114 after
the fixture was regenerated from the current cooker. Root rotation extraction,
animation events, character-controller ownership, collision-limited application,
joint masks, and additive layers remain open.
