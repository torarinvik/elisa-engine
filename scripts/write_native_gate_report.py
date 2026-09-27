#!/usr/bin/env python3
"""Write the structured result for the ElisaScript native-first gate."""

import json
import hashlib
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path


def file_identity(path: Path) -> dict | None:
    if not path.is_file():
        return None
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"path": str(path.resolve()), "sha256": digest.hexdigest()}


def command_output(arguments: list[str]) -> str | None:
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, check=False, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def native_build_identity(root: Path) -> dict:
    manifest_path = root / "native/dependency-manifest.json"
    if not manifest_path.is_file():
        return {}
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = {entry["name"]: entry for entry in manifest.get("libraries", [])}
    wicked_entry = entries.get("WickedEngine", {})
    wicked_value = os.environ.get(wicked_entry.get("path_environment", ""), "") or wicked_entry.get("path_default", "")
    wicked_root = Path(wicked_value).expanduser()
    if not wicked_root.is_absolute():
        wicked_root = root / wicked_root
    wicked_root = wicked_root.resolve()
    build_config = manifest.get("build", {})
    cache_config = build_config.get("wicked_cache", {})
    wicked_build_value = os.environ.get("WICKED_BUILD", "")
    wicked_build = Path(wicked_build_value).expanduser() if wicked_build_value else wicked_root / Path(cache_config.get("path", "build-elisa-sdl3")).parent
    if not wicked_build.is_absolute():
        wicked_build = root / wicked_build
    wicked_build = wicked_build.resolve()
    wicked_git = command_output(["git", "-C", str(wicked_root), "rev-parse", "HEAD"])
    cache_path = Path(cache_config.get("path", "CMakeCache.txt"))
    cache = wicked_build / cache_path.name if wicked_build_value else wicked_root / cache_path

    sdl_entry = entries.get("SDL3", {})
    sdl_library_value = os.environ.get(sdl_entry.get("path_environment", ""), "") or sdl_entry.get("path_default", "")
    sdl_library_root = Path(sdl_library_value).expanduser()
    if not sdl_library_root.is_absolute():
        sdl_library_root = root / sdl_library_root
    sdl_library = sdl_library_root / "libSDL3.dylib"
    sdl_include_value = os.environ.get(sdl_entry.get("include_environment", ""), "") or sdl_entry.get("include_default", "")
    sdl_include = Path(sdl_include_value).expanduser()
    if not sdl_include.is_absolute():
        sdl_include = root / sdl_include
    if not (sdl_include / "SDL_version.h").is_file() and (sdl_include / "SDL3").is_dir():
        sdl_include /= "SDL3"
    version = None
    version_header = sdl_include / "SDL_version.h"
    if version_header.is_file():
        version_text = version_header.read_text(encoding="utf-8", errors="replace")
        components = [re.search(rf"^#define SDL_{part}_VERSION\s+(\d+)", version_text, re.MULTILINE) for part in ("MAJOR", "MINOR", "MICRO")]
        if all(components):
            version = ".".join(match.group(1) for match in components)

    xcrun = shutil.which("xcrun") or "/usr/bin/xcrun"
    sdk_path = command_output([xcrun, "--sdk", "macosx", "--show-sdk-path"])
    sdk_version = command_output([xcrun, "--sdk", "macosx", "--show-sdk-version"])
    cxx = shlex.split(os.environ.get("CXX", "/usr/bin/clang++"))
    cxx_path = shutil.which(cxx[0]) if cxx else None
    compiler_version = command_output([cxx_path or cxx[0], "--version"]) if cxx else None
    validation = root / "build/validation.json"
    validation_identity = file_identity(validation)
    if validation_identity:
        try:
            validation_data = json.loads(validation.read_text(encoding="utf-8"))
            validation_identity["status"] = validation_data.get("status")
            validation_identity["engine"] = validation_data.get("engine", {}).get("git")
            validation_identity["tools"] = validation_data.get("tools", {})
        except (OSError, json.JSONDecodeError):
            validation_identity["status"] = "unreadable"

    return {
        "wicked": {
            "root": str(wicked_root),
            "commit": wicked_git,
            "cmake_cache": file_identity(cache),
            "engine_archive": file_identity(wicked_build / "WickedEngine/libWickedEngine.a"),
            "jolt_archive": file_identity(wicked_build / "WickedEngine/libJolt.a"),
        },
        "sdl3": {"version": version, "library": file_identity(sdl_library), "include": str(sdl_include.resolve())},
        "sdk": {"path": sdk_path, "version": sdk_version, "developer_dir": os.environ.get("DEVELOPER_DIR", "")},
        "cxx": {"path": cxx_path or (cxx[0] if cxx else None), "version": compiler_version},
        "shared_validation": validation_identity,
    }


def main() -> int:
    if len(sys.argv) != 9:
        print("usage: write_native_gate_report.py OUTPUT MODE DEP SOURCE HYGIENE HEADLESS APPLICATION NATIVE", file=sys.stderr)
        return 2
    output, mode = Path(sys.argv[1]), sys.argv[2]
    names = ("dependency", "source_length", "module_hygiene", "headless", "application", "native")
    try:
        statuses = {name: int(value) for name, value in zip(names, sys.argv[3:])}
    except ValueError:
        print("native gate statuses must be integers", file=sys.stderr)
        return 2
    def stage(value: int) -> dict:
        return {"status": value, "state": "pass" if value == 0 else "skip" if value < 0 else "fail"}

    checkout = output.parent.parent
    revision = subprocess.run(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    report = {
        "schema": 2,
        "mode": mode,
        "outcome": "pass" if all(value <= 0 for value in statuses.values()) else "fail",
        "hardware_verification": "verified" if mode == "native" and statuses["application"] == 0 and statuses["native"] == 0 else "unverified",
        "recorded_at_unix": time.time(),
        "provenance": {
            "checkout": str(checkout),
            "git_revision": revision.stdout.strip() if revision.returncode == 0 else "unknown",
            "platform": platform.platform(),
            "python": platform.python_version(),
            "developer_dir": os.environ.get("DEVELOPER_DIR", ""),
        },
        "native_build": native_build_identity(checkout),
        "stages": {name: stage(value) for name, value in statuses.items()},
        # Keep flat status fields for existing consumers.
        **statuses,
    }
    output.write_text(json.dumps(report, sort_keys=True) + "\n", encoding="utf-8")
    print(f"native gate report: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
