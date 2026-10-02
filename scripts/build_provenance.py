#!/usr/bin/env python3
"""Build identity records for Elisa application outputs.

The sidecar describes source checkouts and tool inputs observed by the build
runner. It is intentionally separate from the application's runtime state.
"""

from __future__ import annotations

import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from typing import Iterable


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def optional_sha256(path: Path) -> str | None:
    try:
        return sha256_file(path) if path.is_file() else None
    except OSError:
        return None


def _git(path: Path, *args: str, binary: bool = False):
    try:
        result = subprocess.run(["git", "-C", str(path), *args], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return None
    return result.stdout if binary else result.stdout.decode("utf-8", errors="replace").strip()


def repository_identity(path: Path) -> dict[str, object] | None:
    """Return a commit and a compact fingerprint of local source changes."""
    root_value = _git(path, "rev-parse", "--show-toplevel")
    commit = _git(path, "rev-parse", "HEAD")
    if not root_value or not commit:
        return None
    root = Path(root_value).resolve()
    branch = _git(root, "rev-parse", "--abbrev-ref", "HEAD")
    status = _git(root, "status", "--porcelain=v1", "--untracked-files=all") or ""
    diff = _git(root, "diff", "--binary", "HEAD", binary=True) or b""
    untracked_value = _git(root, "ls-files", "--others", "--exclude-standard", "-z", binary=True) or b""
    untracked = []
    for raw in untracked_value.split(b"\0"):
        if not raw:
            continue
        relative = raw.decode("utf-8", errors="replace")
        candidate = root / relative
        item: dict[str, object] = {"path": relative}
        try:
            if candidate.is_file():
                item["sha256"] = sha256_file(candidate)
                item["size_bytes"] = candidate.stat().st_size
            else:
                item["kind"] = "non-file"
        except OSError:
            item["kind"] = "unreadable"
        untracked.append(item)
    return {
        "root": str(root),
        "commit": commit,
        "branch": branch,
        "dirty": bool(status),
        "tracked_diff_sha256": hashlib.sha256(diff).hexdigest(),
        "untracked_files": untracked,
    }


def executable_identity(command: str) -> dict[str, object]:
    requested = command
    candidate = Path(command).expanduser()
    located = candidate if candidate.is_file() else None
    if located is None:
        resolved = shutil.which(command)
        located = Path(resolved) if resolved else None
    if located is None:
        return {"requested": requested, "resolved": None, "sha256": None}
    located = located.resolve()
    try:
        return {"requested": requested, "resolved": str(located),
            "sha256": sha256_file(located), "size_bytes": located.stat().st_size}
    except OSError:
        return {"requested": requested, "resolved": str(located), "sha256": None}


def artifact_identities(paths: Iterable[Path]) -> list[dict[str, object]]:
    artifacts = []
    seen = set()
    for raw in paths:
        path = raw.expanduser().resolve()
        if path in seen or not path.is_file():
            continue
        seen.add(path)
        try:
            stat = path.stat()
            artifacts.append({"path": str(path), "size_bytes": stat.st_size,
                "sha256": sha256_file(path)})
        except OSError:
            artifacts.append({"path": str(path), "unreadable": True})
    return artifacts


def compute_build_identity(*, project: Path, main_source: Path, engine_root: Path,
    wicked_root: Path, compiler: str, cxx: str, runtime_object: Path,
    native_artifacts: Iterable[Path], options: dict[str, object],
    output: Path | None = None) -> int:
    """Return a stable, compact ID for the source/tool inputs of one build.

    Machine-local checkout paths and the output binary hash are intentionally
    excluded. The full provenance record remains the authoritative explanation
    of this ID; the 63-bit value is suitable for a compact runtime report.
    """
    project = project.resolve()
    repositories: dict[str, dict[str, object] | None] = {}
    roots = [("game", project), ("engine", engine_root), ("wicked", wicked_root),
        ("assets", project / "assets")]
    seen_roots: set[Path] = set()
    generated_outputs = set()
    if output is not None:
        output_path = output.resolve()
        generated_outputs.add(output_path)
        generated_outputs.add(output_path.with_name(output_path.name + ".provenance.json"))
    for label, candidate in roots:
        identity = repository_identity(candidate)
        if identity is None:
            repositories[label] = None
            continue
        root = Path(str(identity["root"]))
        if root in seen_roots:
            repositories[label] = None
            continue
        seen_roots.add(root)
        root = Path(str(identity["root"]))
        untracked = [item for item in identity["untracked_files"]
            if (root / str(item.get("path", ""))).resolve() not in generated_outputs]
        empty_diff = hashlib.sha256(b"").hexdigest()
        repositories[label] = {
            "commit": identity["commit"],
            "dirty": identity["tracked_diff_sha256"] != empty_diff or bool(untracked),
            "tracked_diff_sha256": identity["tracked_diff_sha256"],
            "untracked_files": untracked,
        }

    tools = {
        "elisa_compiler_sha256": executable_identity(compiler).get("sha256"),
        "native_compiler_sha256": executable_identity(cxx).get("sha256"),
        "runtime_object_sha256": optional_sha256(runtime_object),
    }
    artifacts = [{
        "name": Path(str(item.get("path", ""))).name,
        "sha256": item.get("sha256"),
        "size_bytes": item.get("size_bytes"),
    } for item in artifact_identities(native_artifacts)]
    identity_options = {key: options.get(key) for key in (
        "native_optimize", "public_runtime", "native_test_probes", "force_cook_assets")}
    payload = {
        "schema": 1,
        "project_manifest_sha256": optional_sha256(project / "elisa.project.json"),
        "dependency_manifest_sha256": optional_sha256(project / "config/dependencies.json"),
        "main_source_sha256": sha256_file(main_source),
        "repositories": repositories,
        "tools": tools,
        "native_link_artifacts": artifacts,
        "options": identity_options,
    }
    encoded = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    identity = int.from_bytes(hashlib.sha256(encoded).digest()[:8], "big") & ((1 << 63) - 1)
    return identity if identity != 0 else 1


def write_build_provenance(*, output: Path, project: Path, main_source: Path,
    engine_root: Path, wicked_root: Path, compiler: str, cxx: str,
    runtime_object: Path, native_artifacts: Iterable[Path], options: dict[str, object],
    build_identity: int | None = None) -> Path:
    """Write an atomic provenance sidecar next to a successfully linked binary."""
    project = project.resolve()
    native_artifacts = tuple(native_artifacts)
    repositories: dict[str, dict[str, object] | None] = {}
    roots = [("game", project), ("engine", engine_root), ("wicked", wicked_root),
        ("assets", project / "assets")]
    seen_roots: set[Path] = set()
    for label, candidate in roots:
        identity = repository_identity(candidate)
        if identity is None:
            repositories[label] = None
            continue
        root = Path(str(identity["root"]))
        if root in seen_roots:
            repositories[label] = None
            continue
        seen_roots.add(root)
        repositories[label] = identity

    if build_identity is None:
        build_identity = compute_build_identity(project=project, main_source=main_source,
            engine_root=engine_root, wicked_root=wicked_root, compiler=compiler, cxx=cxx,
            runtime_object=runtime_object, native_artifacts=native_artifacts, options=options,
            output=output)
    if build_identity <= 0 or build_identity >= (1 << 63):
        raise ValueError("build identity must be a nonzero 63-bit integer")
    record = {
        "schema": 2,
        "build_identity": f"{build_identity:016x}",
        "built_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "project_root": str(project),
        "main_source": str(main_source.resolve()),
        "project_manifest_sha256": optional_sha256(project / "elisa.project.json"),
        "dependency_manifest_sha256": optional_sha256(project / "config/dependencies.json"),
        "main_source_sha256": sha256_file(main_source),
        "repositories": repositories,
        "tools": {
            "elisa_compiler": executable_identity(compiler),
            "native_compiler": executable_identity(cxx),
            "runtime_object": {"path": str(runtime_object.resolve()),
                "sha256": sha256_file(runtime_object)},
        },
        "native_link_artifacts": artifact_identities(native_artifacts),
        "options": options,
        "binary": {"path": str(output.resolve()), "sha256": sha256_file(output),
            "size_bytes": output.stat().st_size},
    }
    destination = output.with_name(output.name + ".provenance.json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{destination.name}.",
        suffix=".tmp", dir=destination.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(record, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, destination)
    finally:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
    return destination
