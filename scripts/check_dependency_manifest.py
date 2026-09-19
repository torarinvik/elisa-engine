"""Validate native dependency identities and the active SDL3 link boundary."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def git_head(path: Path) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    return result.stdout.strip() if result.returncode == 0 else None


def resolved_path(root: Path, entry: dict, environment_key: str = "path_environment") -> Path:
    configured = os.environ.get(entry.get(environment_key, ""), "")
    value = configured or entry["path_default"]
    path = Path(value).expanduser()
    return path if path.is_absolute() else (root / path).resolve()


def artifact_path(base: Path, artifact: str | dict) -> tuple[Path, str | None]:
    if isinstance(artifact, str):
        return base / artifact, None
    return base / artifact["path"], artifact.get("sha256")


def main(arguments: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path)
    options = parser.parse_args(arguments)
    root = options.root.resolve()
    manifest_path = (options.manifest or root / "native/dependency-manifest.json").resolve()
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    if document.get("schema") != 1:
        raise SystemExit("dependency manifest schema is unsupported")
    failures: list[str] = []
    names: set[str] = set()
    for entry in document.get("libraries", []):
        name = entry.get("name", "")
        if not name or name in names:
            failures.append(f"duplicate or empty library name: {name!r}")
            continue
        names.add(name)
        base = resolved_path(root, entry)
        if not base.is_dir():
            failures.append(f"{name}: dependency directory is missing: {base}")
            continue
        if entry.get("kind") == "git":
            actual = git_head(base)
            expected = entry.get("revision")
            if actual != expected:
                failures.append(f"{name}: checkout {actual!r}, expected {expected!r}")
            if name == "WickedEngine":
                cache = base / document["build"]["wicked_cache"]["path"]
                if not cache.is_file():
                    failures.append(f"{name}: configured CMake cache is missing: {cache}")
                else:
                    cache_text = cache.read_text(encoding="utf-8")
                    for key, expected_value in document["build"]["wicked_cache"]["values"].items():
                        marker = f"{key}:BOOL={expected_value}"
                        if marker not in cache_text:
                            failures.append(f"{name}: CMake cache lacks {marker}")
        for artifact in entry.get("artifacts", []):
            path, expected_hash = artifact_path(base, artifact)
            if not path.is_file():
                failures.append(f"{name}: required artifact is missing: {path}")
            elif expected_hash and digest(path) != expected_hash:
                failures.append(f"{name}: hash drift in {path}")
        include_default = entry.get("include_default")
        if include_default:
            include_entry = {
                "path_default": include_default,
                "path_environment": entry.get("include_environment", ""),
            }
            include_base = resolved_path(root, include_entry, "include_environment")
            for artifact in entry.get("include_artifacts", []):
                include_path = include_base / artifact
                if not include_path.is_file():
                    failures.append(f"{name}: include artifact is missing: {include_path}")

    active_files = [root / "native/wicked_probe.cpp", root / "scripts/wicked_probe.elisascript"]
    forbidden = [str(value).lower() for value in document["build"]["forbidden_link_names"]]
    for path in active_files:
        text = path.read_text(encoding="utf-8").lower()
        for token in forbidden:
            if token in text:
                failures.append(f"active native target contains forbidden SDL2 token {token!r}: {path}")
    flags = set(document["build"]["required_flags"])
    driver_text = (root / document["driver"]).read_text(encoding="utf-8")
    for flag in flags:
        if flag not in driver_text:
            failures.append(f"native driver is missing required build flag {flag!r}")
    if failures:
        for failure in failures:
            print(f"dependency manifest: {failure}", file=sys.stderr)
        return 1
    print(f"Native dependency manifest passed: {len(names)} libraries, SDL3-only active target.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
