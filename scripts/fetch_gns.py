"""Fetch and build the pinned GameNetworkingSockets static library (git-ignored).

The plan selects GameNetworkingSockets as the transport while replication,
authority, prediction, and rollback stay Elisa work. Only the library source is
pinned here; OpenSSL and Protobuf come from Homebrew (install `protobuf` and
`openssl@3`). The build disables Steam sockets, so the library speaks plain
UDP and no Steam SDK is needed.

Usage:
  python3 scripts/fetch_gns.py
"""

import shutil
import subprocess
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
GNS_DIR = DEPENDENCIES / "gns"
BUILD_DIR = GNS_DIR / "build"

GNS_REPO = "https://github.com/ValveSoftware/GameNetworkingSockets.git"
GNS_COMMIT = "a424b7db649438acafb60c99cae6667587c42732"
GNS_LIB = BUILD_DIR / "src/libGameNetworkingSockets_s.a"


def run(arguments: list[str]) -> str:
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"command failed ({' '.join(arguments)}): {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def ensure_checkout() -> None:
    if not (GNS_DIR / ".git").is_dir():
        GNS_DIR.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--depth", "1", GNS_REPO, str(GNS_DIR)])
    head = run(["git", "-C", str(GNS_DIR), "rev-parse", "HEAD"]).strip()
    if head != GNS_COMMIT:
        raise SystemExit(
            f"GameNetworkingSockets checkout is {head}, expected {GNS_COMMIT}; "
            f"remove {GNS_DIR} and re-run to reset")


def ensure_built() -> Path:
    if GNS_LIB.is_file():
        return GNS_LIB
    if shutil.which("protoc") is None:
        raise SystemExit("install Protobuf (brew install protobuf) before building GameNetworkingSockets")
    run([
        "cmake", "-S", str(GNS_DIR), "-B", str(BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIB=OFF", "-DBUILD_STATIC_LIB=ON",
        "-DUSE_STEAM_SOCKETS=OFF",
    ])
    run(["cmake", "--build", str(BUILD_DIR), "--target", "GameNetworkingSockets_s"])
    if not GNS_LIB.is_file():
        raise SystemExit(f"GameNetworkingSockets build did not produce {GNS_LIB}")
    return GNS_LIB


def main() -> int:
    ensure_checkout()
    library = ensure_built()
    print(f"GameNetworkingSockets {GNS_COMMIT[:12]} ready:")
    print(f"  {library.relative_to(ENGINE_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
