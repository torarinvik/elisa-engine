#!/usr/bin/env python3
"""Build and run the sanitizer boundary harness.

The full native probe links Wicked and needs a display session, which the
sandbox aborts when instrumented. This harness exercises the untrusted-boundary
libraries (ozz sampling, Recast/Detour navigation, miniaudio decode,
FreeType/HarfBuzz shaping) without a renderer, so AddressSanitizer and
UndefinedBehaviorSanitizer can run on that boundary anywhere. It then runs the
asset worker harness, the thread behind asynchronous snapshot asset requests,
under ThreadSanitizer. A nonzero exit status is a sanitizer finding or a
failing check.

Requires `python3 scripts/fetch_ozz.py` and `python3 scripts/fetch_recast.py`,
plus Homebrew freetype and harfbuzz.

Usage:
  python3 scripts/run_boundary_sanitized.py
"""

import os
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"


def require(path: Path, hint: str) -> None:
    if not path.is_file():
        raise SystemExit(f"missing {path}; {hint}")


def run_asset_worker_tsan() -> int:
    output = ENGINE_ROOT / "build/asset-worker-harness-tsan"
    arguments = [
        "c++", "-std=c++17", "-O1", "-g", "-fsanitize=thread",
        "-I", str(ENGINE_ROOT / "native"),
        str(ENGINE_ROOT / "native/asset_worker_harness.cpp"),
        "-o", str(output),
    ]
    compile_result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if compile_result.returncode != 0:
        sys.stderr.write(compile_result.stderr)
        return compile_result.returncode
    environment = dict(os.environ)
    environment["TSAN_OPTIONS"] = "halt_on_error=1"
    run_result = subprocess.run([str(output)], capture_output=True, text=True, check=False, env=environment)
    sys.stdout.write(run_result.stdout)
    sys.stderr.write(run_result.stderr)
    if run_result.returncode != 0:
        return run_result.returncode
    print("asset worker harness passed: no ThreadSanitizer finding")
    return 0


def main() -> int:
    brew_include = Path(os.environ.get("HOMEBREW_INCLUDE_DIR", "/opt/homebrew/include"))
    brew_library = Path(os.environ.get("HOMEBREW_LIBRARY_DIR", "/opt/homebrew/lib"))
    ozz = DEPENDENCIES / "ozz"
    recast = DEPENDENCIES / "recast"
    miniaudio = DEPENDENCIES / "miniaudio"
    require(miniaudio / "miniaudio.h", "run python3 scripts/fetch_dependencies.py first")
    ozz_libs = [
        ozz / "build/src/animation/offline/libozz_animation_offline_r.a",
        ozz / "build/src/animation/runtime/libozz_animation_r.a",
        ozz / "build/src/base/libozz_base_r.a",
    ]
    recast_libs = [recast / "build/Recast/libRecast.a", recast / "build/Detour/libDetour.a"]
    for lib in ozz_libs:
        require(lib, "run python3 scripts/fetch_ozz.py first")
    for lib in recast_libs:
        require(lib, "run python3 scripts/fetch_recast.py first")
    output = ENGINE_ROOT / "build/boundary-harness-asan"
    output.parent.mkdir(parents=True, exist_ok=True)
    arguments = [
        "c++", "-std=c++17", "-O1", "-g",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-I", str(ozz / "include"),
        "-I", str(recast / "Recast/Include"),
        "-I", str(recast / "Detour/Include"),
        "-I", str(miniaudio),
        "-I", str(brew_include / "freetype2"),
        "-I", str(brew_include / "harfbuzz"),
        str(ENGINE_ROOT / "native/boundary_harness.cpp"),
        str(ENGINE_ROOT / "native/miniaudio_implementation.cpp"),
        *(str(lib) for lib in ozz_libs),
        *(str(lib) for lib in recast_libs),
        "-L", str(brew_library), "-lfreetype", "-lharfbuzz", "-lzstd",
        "-framework", "CoreFoundation", "-framework", "CoreAudio",
        "-framework", "AudioToolbox", "-framework", "Foundation",
        "-o", str(output),
    ]
    compile_result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if compile_result.returncode != 0:
        sys.stderr.write(compile_result.stderr)
        return compile_result.returncode
    environment = dict(os.environ)
    environment["ASAN_OPTIONS"] = "detect_leaks=0:abort_on_error=1"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1"
    run_result = subprocess.run([str(output)], capture_output=True, text=True, check=False, env=environment)
    sys.stdout.write(run_result.stdout)
    sys.stderr.write(run_result.stderr)
    if run_result.returncode != 0:
        return run_result.returncode
    print("sanitized boundary harness passed: no AddressSanitizer or UBSan finding")
    return run_asset_worker_tsan()


if __name__ == "__main__":
    raise SystemExit(main())
