#!/usr/bin/env bash
# Build a navigation test that links the Recast/Detour navigation service
# (native/navigation_service_abi.cpp). Usage: scripts/build_nav_native_test.sh
# NAME builds test/NAME.elisa into build/NAME-test.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
NAME="${1:?test name}"
COMPILER="${ELISA_COMPILER_BIN:-elisac-stage1}"
RUNTIME="${ELISA_RUNTIME_OBJECT:-$HOME/.elisac/stage1/build/runtime/elisacore_runtime.o}"
RECAST="$ROOT/dependencies/recast"
mkdir -p "$ROOT/build"
clang++ -c -std=c++17 -O2 -Wall -Wextra -I "$RECAST/Recast/Include" -I "$RECAST/Detour/Include" \
  -o "$ROOT/build/navigation_service_abi.o" "$ROOT/native/navigation_service_abi.cpp"
clang++ -c -std=c++17 -O2 -o "$ROOT/build/elisa_native_fallbacks.o" "$ROOT/native/elisa_native_fallbacks.cpp"
"$COMPILER" -emit obj -o "$ROOT/build/$NAME.o" "$ROOT/test/$NAME.elisa"
clang++ -o "$ROOT/build/$NAME-test" "$ROOT/build/$NAME.o" "$ROOT/build/navigation_service_abi.o" \
  "$ROOT/build/elisa_native_fallbacks.o" "$RUNTIME" \
  "$RECAST/build/Recast/libRecast.a" "$RECAST/build/Detour/libDetour.a"
echo "built $ROOT/build/$NAME-test"
