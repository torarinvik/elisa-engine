"""Fetch the pinned Tracy profiler client (git-ignored).

Tracy is multi-file but needs no separate build: the client is compiled into
the native probe from `public/TracyClient.cpp` with `-DTRACY_ENABLE`. This
script clones the pinned commit so the source is reproducible.

Usage:
  python3 scripts/fetch_tracy.py
"""

import subprocess
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
TRACY_DIR = DEPENDENCIES / "tracy"

TRACY_REPO = "https://github.com/wolfpld/tracy.git"
TRACY_TAG = "v0.14.1"
TRACY_COMMIT = "30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c"


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(arguments)}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def main() -> int:
    if not (TRACY_DIR / ".git").is_dir():
        TRACY_DIR.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--branch", TRACY_TAG, "--depth", "1", TRACY_REPO, str(TRACY_DIR)])
    head = run(["git", "-C", str(TRACY_DIR), "rev-parse", "HEAD"]).strip()
    if head != TRACY_COMMIT:
        raise SystemExit(
            f"tracy checkout is {head}, expected {TRACY_COMMIT}; remove {TRACY_DIR} and re-run to reset")
    client = TRACY_DIR / "public/TracyClient.cpp"
    if not client.is_file():
        raise SystemExit(f"tracy client is missing: {client}")
    print(f"tracy {TRACY_TAG} ({TRACY_COMMIT[:12]}) ready:")
    print(f"  {client.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
