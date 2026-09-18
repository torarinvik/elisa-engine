#!/usr/bin/env python3
"""Build and run the real GameNetworkingSockets transport probe.

Fetches and builds the pinned library (scripts/fetch_gns.py), compiles
native/gns_probe.cpp against it, and requires the engine's 33-byte
replication frame to arrive over a loopback connection unchanged. This is the
selected transport library itself, not a stand-in; replication, authority, and
rollback stay engine work above it.

Requires Homebrew protobuf and openssl@3.

Usage:
  python3 scripts/gns_probe.py
"""

import os
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = ENGINE_ROOT / "dependencies"
GNS = DEPENDENCIES / "gns"
MARKER = "gns transport:"


def relay(result: subprocess.CompletedProcess) -> None:
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)


def main() -> int:
    brew_prefix = Path(os.environ.get("HOMEBREW_PREFIX", "/opt/homebrew"))
    fetch = subprocess.run(
        [sys.executable, str(ENGINE_ROOT / "scripts/fetch_gns.py")],
        capture_output=True, text=True, check=False,
    )
    relay(fetch)
    if fetch.returncode != 0:
        return fetch.returncode

    build = ENGINE_ROOT / "build"
    build.mkdir(parents=True, exist_ok=True)
    probe = build / "gns-probe"
    compile_result = subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++17", "-O1",
        "-I", str(ENGINE_ROOT / "native"),
        "-I", str(GNS / "include"),
        "-I", str(GNS / "build/src"),
        str(ENGINE_ROOT / "native/gns_probe.cpp"),
        str(GNS / "build/src/libGameNetworkingSockets_s.a"),
        "-L", str(brew_prefix / "lib"), "-lprotobuf", "-lssl", "-lcrypto",
        "-o", str(probe),
    ], capture_output=True, text=True, check=False)
    relay(compile_result)
    if compile_result.returncode != 0 or not probe.is_file():
        print("gns probe: compile failed", file=sys.stderr)
        return compile_result.returncode if compile_result.returncode != 0 else 1

    run = subprocess.run([str(probe)], capture_output=True, text=True, check=False)
    relay(run)
    if run.returncode != 0 or MARKER not in run.stdout:
        print("gns probe failed; the log identifies the step.", file=sys.stderr)
        return run.returncode if run.returncode != 0 else 1
    print("GameNetworkingSockets moved the engine's replication frame over a real connection.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
