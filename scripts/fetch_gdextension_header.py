#!/usr/bin/env python3
"""Dump the version-matched GDExtension interface header (git-ignored).

godot-cpp publishes no branch for this Godot, so the hand-written bridge binds
directly against the C interface. The installed binary emits its own header via
--dump-gdextension-interface, so provenance is the Godot version itself rather
than an external pin: the dump is re-run before every extension build.

Usage:
  python3 scripts/fetch_gdextension_header.py
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[1]
TARGET_DIR = ENGINE_ROOT / "dependencies" / "gdextension"
HEADER_NAME = "gdextension_interface.h"


def find_godot() -> str:
    configured = os.environ.get("GODOT_BIN", "")
    if configured:
        return configured
    located = shutil.which("godot")
    if located is None:
        raise SystemExit("Install Godot or set GODOT_BIN to its executable path.")
    return located


def main() -> int:
    godot = find_godot()
    workdir = Path(tempfile.mkdtemp(prefix="elisa-gdext-header-"))
    try:
        (workdir / "project.godot").write_text(
            'config_version=5\n\n[application]\nconfig/name="elisa-gdext-header"\n',
            encoding="utf-8",
        )
        result = subprocess.run(
            [godot, "--headless", "--path", str(workdir), "--dump-gdextension-interface"],
            capture_output=True, text=True, check=False,
        )
        header = workdir / HEADER_NAME
        if result.returncode != 0 or not header.is_file():
            print(result.stdout, file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            raise SystemExit("godot failed to dump the GDExtension interface header")
        TARGET_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copy2(header, TARGET_DIR / HEADER_NAME)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    print(f"gdextension header ready: dependencies/gdextension/{HEADER_NAME}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
