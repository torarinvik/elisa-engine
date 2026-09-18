"""Fetch and build the pinned Recast/Detour navigation libraries (git-ignored).

Like ozz, this dependency is multi-file and CMake-built, so it is pinned by
commit and built into dependencies/recast/build. Only Recast and Detour are
built; the demo, tests, and examples stay off. `CMAKE_POLICY_VERSION_MINIMUM`
is set because the upstream CMakeLists predates the policy floor of newer CMake,
which otherwise refuses to configure it.

Usage:
  python3 scripts/fetch_recast.py
"""

import subprocess
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
RECAST_DIR = DEPENDENCIES / "recast"
BUILD_DIR = RECAST_DIR / "build"

RECAST_REPO = "https://github.com/recastnavigation/recastnavigation.git"
RECAST_TAG = "v1.6.0"
RECAST_COMMIT = "6dc1667f580357e8a2154c28b7867bea7e8ad3a7"

RECAST_LIBS = ("Recast/libRecast.a", "Detour/libDetour.a")


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(arguments)}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def ensure_checkout() -> None:
    if not (RECAST_DIR / ".git").is_dir():
        RECAST_DIR.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--branch", RECAST_TAG, "--depth", "1", RECAST_REPO, str(RECAST_DIR)])
    head = run(["git", "-C", str(RECAST_DIR), "rev-parse", "HEAD"]).strip()
    if head != RECAST_COMMIT:
        raise SystemExit(
            f"recast checkout is {head}, expected {RECAST_COMMIT}; remove {RECAST_DIR} and re-run to reset")


def ensure_built() -> list[Path]:
    libs = [BUILD_DIR / relative for relative in RECAST_LIBS]
    if all(lib.is_file() for lib in libs):
        return libs
    run([
        "cmake", "-S", str(RECAST_DIR), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DRECASTNAVIGATION_DEMO=OFF", "-DRECASTNAVIGATION_TESTS=OFF", "-DRECASTNAVIGATION_EXAMPLES=OFF",
    ])
    run(["cmake", "--build", str(BUILD_DIR), "--target", "Recast", "Detour"])
    missing = [lib for lib in libs if not lib.is_file()]
    if missing:
        raise SystemExit(f"recast build did not produce: {', '.join(str(lib) for lib in missing)}")
    return libs


def main() -> int:
    ensure_checkout()
    libs = ensure_built()
    print(f"recastnavigation {RECAST_TAG} ({RECAST_COMMIT[:12]}) ready:")
    for lib in libs:
        print(f"  {lib.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
