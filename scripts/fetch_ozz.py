"""Fetch and build the pinned ozz-animation runtime (git-ignored).

ozz is multi-file and CMake-built, unlike the single-header dependencies, so it
is pinned by commit and built into dependencies/ozz/build. Only the runtime and
offline animation libraries the native probe links are built; tools, samples,
and howtos stay off so the offline dependency does not grow a larger build than
the engine uses.

Usage:
  python3 scripts/fetch_ozz.py
"""

import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
OZZ_DIR = DEPENDENCIES / "ozz"
BUILD_DIR = OZZ_DIR / "build"

OZZ_REPO = "https://github.com/guillaumeblanc/ozz-animation.git"
OZZ_TAG = "0.16.0"
OZZ_COMMIT = "6cbdc790123aa4731d82e255df187b3a8a808256"

OZZ_LIBS = (
    "src/base/libozz_base_r.a",
    "src/animation/runtime/libozz_animation_r.a",
    "src/animation/offline/libozz_animation_offline_r.a",
)


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(arguments)}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def ensure_checkout() -> None:
    if not (OZZ_DIR / ".git").is_dir():
        OZZ_DIR.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--branch", OZZ_TAG, "--depth", "1", OZZ_REPO, str(OZZ_DIR)])
    head = run(["git", "-C", str(OZZ_DIR), "rev-parse", "HEAD"]).strip()
    if head != OZZ_COMMIT:
        raise SystemExit(
            f"ozz checkout is {head}, expected {OZZ_COMMIT}; remove {OZZ_DIR} and re-run to reset")


def ensure_built() -> list[Path]:
    libs = [BUILD_DIR / relative for relative in OZZ_LIBS]
    if all(lib.is_file() for lib in libs):
        return libs
    run([
        "cmake", "-S", str(OZZ_DIR), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release",
        "-Dozz_build_tools=OFF", "-Dozz_build_samples=OFF", "-Dozz_build_howtos=OFF",
        "-Dozz_build_tests=OFF",
    ])
    run(["cmake", "--build", str(BUILD_DIR), "--target", "ozz_animation", "ozz_animation_offline"])
    missing = [lib for lib in libs if not lib.is_file()]
    if missing:
        raise SystemExit(f"ozz build did not produce: {', '.join(str(lib) for lib in missing)}")
    return libs


def main() -> int:
    ensure_checkout()
    libs = ensure_built()
    print(f"ozz {OZZ_TAG} ({OZZ_COMMIT[:12]}) ready:")
    for lib in libs:
        print(f"  {lib.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
