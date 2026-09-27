# KTX2 mip-chain upload

**Date:** 2026-09-24; dedicated Wicked gate rerun 2026-09-27
**Scope:** Preserve mip chains from Basis KTX2 cooking through CPU decode and the Wicked texture descriptor.

The test cooker now generates complete `4x4 → 2x2 → 1x1` UASTC chains for a 2D image and a six-face cubemap. The Basis probe checks each mip's dimensions and transcodes every level and face. The production uploader also checks that the created Wicked texture retains the source dimensions, face count, and mip count.

## Validation

- `/opt/homebrew/bin/python3 scripts/basisu_probe.py` passed. It reported three levels for the cube and passed all face/mip transcodes, along with the existing alpha, normal-map, swizzle, and HDR checks.
- `ELISA_RENDER_SCENE_RENDER_ONLY=1 /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py` passed on SDL3/Metal. This recompiles the production texture uploader and registers the generated mipmapped 2D KTX2 through the RenderScene path.
- The focused `clang++ -fsyntax-only` check passed for the changed dedicated cubemap descriptor assertion.
- The existing native Wicked probe ran `ktx2only` against the newly generated assets and exited 0 on Metal. That binary predates the new cubemap descriptor assertion, so its result establishes successful cubemap upload but not that assertion's runtime evaluation.

The dedicated Wicked gate was rerun against the clean checkout at the exact revision pinned in `native/dependency-manifest.json`. It rebuilt the native bridge and passed the texture probe on macOS 27 with SDL3/Metal. This verified production texture upload, including KTX2 HDR choosing BC6H and the forced RGBA16F fallback retaining values above 1.0, along with ASTC upload and the existing alpha, normal-map, and cubemap paths.

```sh
WICKED_ROOT="$PWD/../amazing-labyrinth-wickedengine" \
WICKED_BUILD="$PWD/../amazing-labyrinth-wickedengine/build-elisa-sdl3" \
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
PYTHON_BIN=/opt/homebrew/bin/python3 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
"$HOME/.local/bin/elisascript" scripts/wicked_probe.elisascript texture
```

Non-Metal devices and broader HDR/compressed formats remain unverified.

Commands for the focused checks, run from the engine root:

```sh
clang++ -std=c++17 -O0 -include filesystem -DWI_UNORDERED_MAP_TYPE=2 -DWICKED_CMAKE_BUILD -DTRACY_ENABLE -DSDL3=1 -D__OBJC_BOOL_IS_BOOL=1 -I dependencies/cgltf -I build -I dependencies/meshoptimizer -I dependencies/ozz/include -I dependencies/recast/Recast/Include -I dependencies/recast/Detour/Include -I dependencies/miniaudio -I dependencies/basisu/transcoder -I /opt/homebrew/include/freetype2 -I /opt/homebrew/include/harfbuzz -I dependencies/tracy/public -I ../WickedEngine/WickedEngine -I ../WickedEngine/WickedEngine/Utility -I ../WickedEngine/WickedEngine/Utility/metal -I /opt/homebrew/include -I /opt/homebrew/include/SDL3 -fsyntax-only native/wicked_probe.cpp
build/wicked-native-probe "$PWD/../WickedEngine/WickedEngine" "$PWD/backends/scene_manifest.txt" "$PWD/build/wicked-ktx2-upload.png" ktx2only
DEVELOPER_DIR=/Library/Developer/CommandLineTools PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/wicked_probe.elisascript texture
```
