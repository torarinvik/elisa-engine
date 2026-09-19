"""Legacy SDL2 fetcher retained for historical reproducibility only.

The native Wicked host now uses SDL3. New builds must configure Wicked with
``-DWICKED_USE_SDL3=ON`` and install SDL3 through the platform package manager.
This script remains available only for reproducing the pre-SDL3 validation
records and is no longer used by any acceptance gate.

The old Wicked platform layer was built against SDL2, and on Linux that meant real
SDL2 (Homebrew now ships sdl2-compat, a shim that dlopens SDL3 from a library
initializer -- which is also what hangs the AddressSanitizer graphics probe
when the shim cannot load SDL3). The engine's own platform default stays SDL3;
this pin exists so the Wicked host links a real SDL2 everywhere.

Usage:
  python3 scripts/fetch_sdl2.py
"""

import shutil
import subprocess
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
SDL2_DIR = DEPENDENCIES / "sdl2"
BUILD_DIR = SDL2_DIR / "build"

SDL2_REPO = "https://github.com/libsdl-org/SDL.git"
SDL2_TAG = "release-2.32.10"
SDL2_COMMIT = "5d249570393f7a37e037abf22cd6012a4cc56a71"

SDL2_LIB = BUILD_DIR / "libSDL2-2.0.0.dylib"


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(arguments)}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def ensure_checkout() -> None:
    if not (SDL2_DIR / ".git").is_dir():
        SDL2_DIR.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--branch", SDL2_TAG, "--depth", "1", SDL2_REPO, str(SDL2_DIR)])
    head = run(["git", "-C", str(SDL2_DIR), "rev-parse", "HEAD"]).strip()
    if head != SDL2_COMMIT:
        raise SystemExit(
            f"SDL2 checkout is {head}, expected {SDL2_COMMIT}; remove {SDL2_DIR} and re-run to reset")


def ensure_built() -> Path:
    if SDL2_LIB.is_file():
        return SDL2_LIB
    run([
        "cmake", "-S", str(SDL2_DIR), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        "-DSDL_SHARED=ON", "-DSDL_STATIC=OFF", "-DSDL_TEST=OFF",
    ])
    run(["cmake", "--build", str(BUILD_DIR), "--target", "SDL2"])
    if not SDL2_LIB.is_file():
        raise SystemExit(f"SDL2 build did not produce {SDL2_LIB}")
    return SDL2_LIB


def main() -> int:
    ensure_checkout()
    library = ensure_built()
    # The linker looks for libSDL2.dylib; upstream only names libSDL2-2.0.0.
    link_name = BUILD_DIR / "libSDL2.dylib"
    if not link_name.exists():
        link_name.symlink_to("libSDL2-2.0.0.dylib")
    # The probe records @rpath/libSDL2-2.0.0.dylib and links with
    # -Wl,-rpath,@executable_path, so the dylib ships beside the probe.
    staged = ENGINE_ROOT / "build/libSDL2-2.0.0.dylib"
    staged.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(library, staged)
    print(f"SDL2 {SDL2_TAG} ({SDL2_COMMIT[:12]}) ready:")
    print(f"  {library.relative_to(ENGINE_ROOT)}")
    print(f"  {staged.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
