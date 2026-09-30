#!/usr/bin/env bash
# Build the batch mocap CLI (tools/mocap_clean.elisa) into build/mocap-clean,
# linking the Metal viewport bridge (native/viewport_metal.m), the folder shim
# and the FBX baker (native/fbx_to_glb.c over the pinned ufbx). Needs macOS
# with Metal; no window or display is used.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
NAME=mocap_clean
COMPILER="${ELISA_COMPILER_BIN:-elisac-stage1}"
RUNTIME="${ELISA_RUNTIME_OBJECT:-$HOME/.elisac/stage1/build/runtime/elisacore_runtime.o}"
mkdir -p "$ROOT/build"
clang -c -fobjc-arc -O2 -Wall -Wextra -Werror -o "$ROOT/build/viewport_metal.o" "$ROOT/native/viewport_metal.m"
clang++ -c -std=c++17 -O2 -o "$ROOT/build/elisa_native_fallbacks.o" "$ROOT/native/elisa_native_fallbacks.cpp"
clang -c -O2 -Wall -Wextra -Werror -o "$ROOT/build/mocap_folder.o" "$ROOT/native/mocap_folder.c"
UFBX="$ROOT/dependencies/ufbx"
[ -f "$UFBX/ufbx.c" ] || { echo "missing ufbx; run python3 scripts/fetch_dependencies.py" >&2; exit 2; }
[ "$ROOT/build/ufbx.o" -nt "$UFBX/ufbx.c" ] || clang -c -std=c99 -O2 -I "$UFBX" -o "$ROOT/build/ufbx.o" "$UFBX/ufbx.c"
clang -c -std=c99 -O2 -Wall -Wextra -Werror -I "$UFBX" -o "$ROOT/build/fbx_to_glb.o" "$ROOT/native/fbx_to_glb.c"
"$COMPILER" -emit obj -o "$ROOT/build/$NAME.o" "$ROOT/tools/$NAME.elisa"
clang -o "$ROOT/build/mocap-clean" "$ROOT/build/$NAME.o" "$ROOT/build/viewport_metal.o" "$ROOT/build/mocap_folder.o" "$ROOT/build/fbx_to_glb.o" "$ROOT/build/ufbx.o" "$ROOT/build/elisa_native_fallbacks.o" "$RUNTIME" \
  -framework Metal -framework IOSurface -framework Foundation
echo "built $ROOT/build/mocap-clean"
