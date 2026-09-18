"""Fetch and build the pinned Basis Universal encoder (git-ignored).

The plan names KTX/Basis for asset cooking and runtime loading/transcoding.
This builds the `basisu` CLI, which the cooker runs to produce a UASTC KTX2
from the cooked pixels; the Basis transcoder is compiled into
native/basisu_probe.cpp directly from the same checkout, so the runtime side
has no external binary dependency.

Usage:
  python3 scripts/fetch_basisu.py
"""

import subprocess
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
BASISU_DIR = DEPENDENCIES / "basisu"
BUILD_DIR = BASISU_DIR / "build"

BASISU_REPO = "https://github.com/BinomialLLC/basis_universal.git"
BASISU_COMMIT = "99f52d63aa6799cbdaecfe977111dc5ec3b31d47"
BASISU_BIN = BASISU_DIR / "bin/basisu"


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(arguments)}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def ensure_checkout() -> None:
    if not (BASISU_DIR / ".git").is_dir():
        BASISU_DIR.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--depth", "1", BASISU_REPO, str(BASISU_DIR)])
    head = run(["git", "-C", str(BASISU_DIR), "rev-parse", "HEAD"]).strip()
    if head != BASISU_COMMIT:
        raise SystemExit(
            f"basis_universal checkout is {head}, expected {BASISU_COMMIT}; "
            f"remove {BASISU_DIR} and re-run to reset")


def ensure_built() -> Path:
    if BASISU_BIN.is_file():
        return BASISU_BIN
    run(["cmake", "-S", str(BASISU_DIR), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release"])
    run(["cmake", "--build", str(BUILD_DIR), "--target", "basisu"])
    if not BASISU_BIN.is_file():
        raise SystemExit(f"basisu build did not produce {BASISU_BIN}")
    return BASISU_BIN


def main() -> int:
    ensure_checkout()
    binary = ensure_built()
    print(f"basis_universal {BASISU_COMMIT[:12]} ready:")
    print(f"  {binary.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
