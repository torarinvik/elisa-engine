"""Shared content identities and atomic metadata writes for asset cookers."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
from typing import Iterable


_LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def content_fingerprint(value: object) -> str:
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
        ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def local_source_closure(sources: Iterable[Path], include_dirs: Iterable[Path]) -> list[Path]:
    """Return local C/C++ translation units and recursively included headers."""
    search_dirs = tuple(path.resolve() for path in include_dirs)
    pending = [path.resolve() for path in sources]
    visited: set[Path] = set()
    while pending:
        source = pending.pop()
        if source in visited:
            continue
        if not source.is_file():
            raise FileNotFoundError(f"cooker source dependency is missing: {source}")
        visited.add(source)
        text = source.read_text(encoding="utf-8", errors="replace")
        for name in _LOCAL_INCLUDE.findall(text):
            candidates = (source.parent / name, *(directory / name for directory in search_dirs))
            include = next((candidate.resolve() for candidate in candidates if candidate.is_file()), None)
            if include is not None and include not in visited:
                pending.append(include)
    return sorted(visited)


def compiler_identity(command: str) -> dict[str, str]:
    """Identify a compiler by its resolved executable and advertised version."""
    parts = shlex.split(command)
    if not parts:
        raise ValueError("compiler command cannot be empty")
    executable = Path(parts[0]).expanduser()
    resolved_value = str(executable.resolve()) if executable.is_file() else shutil.which(parts[0])
    if not resolved_value:
        raise FileNotFoundError(f"cannot find compiler {parts[0]!r}")
    try:
        result = subprocess.run([*parts, "--version"], text=True,
            capture_output=True, check=False, timeout=10)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f"could not identify compiler {parts[0]!r}: {error}") from error
    output = (result.stdout + result.stderr).strip()
    if result.returncode != 0 or not output:
        raise RuntimeError(f"compiler {parts[0]!r} did not provide a usable --version")
    stat = Path(resolved_value).stat()
    return {"command": shlex.join(parts), "executable": resolved_value,
        "version": output[:4096], "size": str(stat.st_size),
        "mtime_ns": str(stat.st_mtime_ns)}


def atomic_write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                prefix=f".{path.name}.", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(value, stream, sort_keys=True, indent=2, ensure_ascii=False)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
