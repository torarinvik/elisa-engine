"""Fetch pinned third-party headers into dependencies/ (git-ignored).

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
    "cgltf": (
        "snapshot-2026-09-18",
        "https://raw.githubusercontent.com/jkuhlmann/cgltf/master/cgltf.h",
        "efb169dee911696b5d35fc8e3f7ea0c56d679debc529eba9ca6aa6443ba9d5e9",
        "cgltf/cgltf.h",
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
