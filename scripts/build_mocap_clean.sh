#!/usr/bin/env bash
# Build a viewport test that links the Metal viewport bridge
# (native/viewport_metal.m). Usage: scripts/build_viewport_native_test.sh NAME
# builds test/NAME.elisa into build/NAME-test. Needs macOS with Metal; no
# window or display is used.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
NAME=mocap_clean
COMPILER="${ELISA_COMPILER_BIN:-elisac-stage1}"
RUNTIME="${ELISA_RUNTIME_OBJECT:-$HOME/.elisac/stage1/build/runtime/elisacore_runtime.o}"
mkdir -p "$ROOT/build"
clang -c -fobjc-arc -O2 -Wall -Wextra -Werror -o "$ROOT/build/viewport_metal.o" "$ROOT/native/viewport_metal.m"
clang++ -c -std=c++17 -O2 -o "$ROOT/build/elisa_native_fallbacks.o" "$ROOT/native/elisa_native_fallbacks.cpp"
clang -c -O2 -Wall -Wextra -Werror -o "$ROOT/build/mocap_folder.o" "$ROOT/native/mocap_folder.c"
"$COMPILER" -emit obj -o "$ROOT/build/$NAME.o" "$ROOT/tools/$NAME.elisa"
clang -o "$ROOT/build/mocap-clean" "$ROOT/build/$NAME.o" "$ROOT/build/viewport_metal.o" "$ROOT/build/mocap_folder.o" "$ROOT/build/elisa_native_fallbacks.o" "$RUNTIME" \
  -framework Metal -framework IOSurface -framework Foundation
echo "built $ROOT/build/mocap-clean"
