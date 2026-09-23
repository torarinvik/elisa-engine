"""Fetch pinned third-party sources and headers into dependencies/ (git-ignored).

Third-party code stays out of the Elisa-owned tree: this script is the
reproducible way to obtain it. The revision and content hash are pinned, so
a fetched header is verifiable and a changed upstream cannot silently
alter the engine's build. Run it before building the native probe.

Usage:
  python3 scripts/fetch_dependencies.py [--only NAME]
"""

import argparse
import hashlib
import sys
import urllib.request
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"

# name -> (revision, url, sha256, destination relative to dependencies/)
PINNED = {
    "ufbx_source": (
        "v0.23.0-fcc5d6ba444cfd3eb80677dba5e37e493941abe5",
        "https://raw.githubusercontent.com/ufbx/ufbx/fcc5d6ba444cfd3eb80677dba5e37e493941abe5/ufbx.c",
        "7d8d6ae4373f71692f295ff49ee0826466306ebcaa80b0e587c13ed047b98cea",
        "ufbx/ufbx.c",
    ),
    "ufbx_header": (
        "v0.23.0-fcc5d6ba444cfd3eb80677dba5e37e493941abe5",
        "https://raw.githubusercontent.com/ufbx/ufbx/fcc5d6ba444cfd3eb80677dba5e37e493941abe5/ufbx.h",
        "942481725372d2ac4da5e77a062b47c20054a3440e7ee09a6043f99fe1f130ed",
        "ufbx/ufbx.h",
    ),
    "ufbx_license": (
        "v0.23.0-fcc5d6ba444cfd3eb80677dba5e37e493941abe5",
        "https://raw.githubusercontent.com/ufbx/ufbx/fcc5d6ba444cfd3eb80677dba5e37e493941abe5/LICENSE",
        "0dd48ebadf52273c736256325c8f078c03c8bb4facee22a4122de0ad3f615391",
        "ufbx/LICENSE",
    ),
    "cgltf": (
        "snapshot-2026-09-18",
        "https://raw.githubusercontent.com/jkuhlmann/cgltf/master/cgltf.h",
        "efb169dee911696b5d35fc8e3f7ea0c56d679debc529eba9ca6aa6443ba9d5e9",
        "cgltf/cgltf.h",
    ),
    "miniaudio": (
        "0.11.22",
        "https://raw.githubusercontent.com/mackron/miniaudio/0.11.22/miniaudio.h",
        "9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410",
        "miniaudio/miniaudio.h",
    ),
    # meshoptimizer is multi-file; each source used by a build or asset cook
    # is pinned independently to keep the offline geometry cooker reproducible.
    "meshoptimizer_header": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/meshoptimizer.h",
        "21a72040a75bacf6ddefb7e74f1cf566af1e68ea5e4dc0db598278f4681e0b87",
        "meshoptimizer/meshoptimizer.h",
    ),
    "meshoptimizer_allocator": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/allocator.cpp",
        "d2cc48691fe2f4c6d097bf7a766389cfb7f83ca14a5f747e3944332656d02254",
        "meshoptimizer/allocator.cpp",
    ),
    "meshoptimizer_vcache": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/vcacheoptimizer.cpp",
        "618429ef4db8ab9b16fde4e73dcf425d89452acbf103085478bc8b1761fa6689",
        "meshoptimizer/vcacheoptimizer.cpp",
    ),
    "meshoptimizer_analyzer": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/indexanalyzer.cpp",
        "bff37aecb10cefa33f3f6a217413c6ac98899c23e6de5282420c60e6baf786ec",
        "meshoptimizer/indexanalyzer.cpp",
    ),
    "meshoptimizer_simplifier": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/simplifier.cpp",
        "dc40aadb307577ed3f7adb5102a506263de8b9fea1d5582a24c04bff2874a2cc",
        "meshoptimizer/simplifier.cpp",
    ),
    "meshoptimizer_vfetch": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/vfetchoptimizer.cpp",
        "aa534bb8150ca27ae229c58a0dc51f85f9acccbc5d4214f4c82edd91fc08e478",
        "meshoptimizer/vfetchoptimizer.cpp",
    ),
    "meshoptimizer_indexgenerator": (
        "v1.2",
        "https://raw.githubusercontent.com/zeux/meshoptimizer/v1.2/src/indexgenerator.cpp",
        "0e971dd8cc2ced68cd0374163461fc6c3e846267aee7b47203837164991a015e",
        "meshoptimizer/indexgenerator.cpp",
    ),
    # Offline mesh authoring stages. Pin source files by immutable upstream commit.
    "mikktspace_source": (
        "3e895b49d05ea07e4c2133156cfa94369e19e409",
        "https://raw.githubusercontent.com/mmikk/MikkTSpace/3e895b49d05ea07e4c2133156cfa94369e19e409/mikktspace.c",
        "de87e74107df766ce68108801262bd8d53899414236b59810509a8fc2a51e288",
        "mikktspace/mikktspace.c",
    ),
    "mikktspace_header": (
        "3e895b49d05ea07e4c2133156cfa94369e19e409",
        "https://raw.githubusercontent.com/mmikk/MikkTSpace/3e895b49d05ea07e4c2133156cfa94369e19e409/mikktspace.h",
        "17fc433894f24c73753d548086cc4d8c5c0379f4a6edfb98b5da243e4f0bc3d0",
        "mikktspace/mikktspace.h",
    ),
    "xatlas_source": (
        "f700c7790aaa030e794b52ba7791a05c085faf0c",
        "https://raw.githubusercontent.com/jpcy/xatlas/f700c7790aaa030e794b52ba7791a05c085faf0c/source/xatlas/xatlas.cpp",
        "0ed0283aad005c94738cb0cc4612dba264379d29dea5b3c9b242f2d4752d5df4",
        "xatlas/xatlas.cpp",
    ),
    "xatlas_header": (
        "f700c7790aaa030e794b52ba7791a05c085faf0c",
        "https://raw.githubusercontent.com/jpcy/xatlas/f700c7790aaa030e794b52ba7791a05c085faf0c/source/xatlas/xatlas.h",
        "e7675335ad8ab1c1cc9060ad153cf6b8ba2ee914282044eb5f02c49590218fbd",
        "xatlas/xatlas.h",
    ),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(name: str, force: bool = False) -> Path:
    (revision, url, expected, relative) = PINNED[name]
    destination = DEPENDENCIES / relative
    if destination.is_file() and not force:
        print(f"{name}: present at {destination} ({sha256(destination.read_bytes())[:12]})")
        return destination
    print(f"{name}: fetching {revision} from {url}")
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()
    # The first fetch records the observed hash; a pinned hash turns later
    # runs into a verification instead of a silent update.
    digest = sha256(data)
    if expected is None:
        print(f"{name}: WARNING no pin recorded; observed sha256={digest}")
        print(f"{name}: add this hash to PINNED in this script to lock it")
    elif digest != expected:
        raise SystemExit(f"{name}: hash mismatch: got {digest}, expected {expected}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    print(f"{name}: wrote {destination} ({digest[:12]})")
    return destination


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", help="fetch one dependency by name")
    parser.add_argument("--force", action="store_true", help="re-fetch even if present")
    options = parser.parse_args(arguments)
    names = [options.only] if options.only else sorted(PINNED)
    for name in names:
        if name not in PINNED:
            print(f"unknown dependency: {name}", file=sys.stderr)
            return 2
        fetch(name, options.force)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
