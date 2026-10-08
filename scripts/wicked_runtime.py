#!/usr/bin/env python3
"""Stage Wicked's executable-relative shader compiler libraries for dev runs."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import tempfile
import sys
from pathlib import Path


RUNTIME_LIBRARIES = ("libdxcompiler.dylib", "libmetalirconverter.dylib")
REQUIRED_RUNTIME_LIBRARIES = frozenset({"libdxcompiler.dylib"})


class WickedRuntimeError(ValueError):
    """A Wicked runtime library cannot be staged beside the executable."""


def stage_wicked_runtime_libraries(executable: Path, wicked_source: Path,
    *, staged_executable: Path | None = None, staged_provenance: Path | None = None) -> list[Path]:
    """Prepare all runtime links, publish them, then publish the executable.

    On failure restore the exact previous link targets. Failed rollback keeps
    its recovery directory so the previous links remain recoverable.
    """
    executable = executable.expanduser().absolute()
    wicked_source = wicked_source.expanduser().resolve()
    candidate = staged_executable if staged_executable is not None else executable
    if not candidate.is_file():
        raise WickedRuntimeError(f"native executable does not exist: {candidate}")
    if executable.name in RUNTIME_LIBRARIES:
        raise WickedRuntimeError(f"executable name is reserved for a Wicked runtime library: {executable}")
    if not wicked_source.is_dir():
        raise WickedRuntimeError(f"Wicked source directory does not exist: {wicked_source}")

    changes: list[tuple[Path, Path | None]] = []
    # Validate both destinations before changing either one.
    for name in RUNTIME_LIBRARIES:
        source = wicked_source / name
        destination = executable.parent / name
        if not source.is_file():
            if name in REQUIRED_RUNTIME_LIBRARIES:
                raise WickedRuntimeError(f"Wicked shader compiler is missing: {source}")
            if destination.is_symlink():
                changes.append((destination, None))
            elif destination.exists():
                raise WickedRuntimeError(
                    f"{source.name} is absent from this Wicked checkout but a file remains beside "
                    f"the executable: {destination}")
            continue
        if destination.is_symlink():
            if destination.resolve() == source:
                continue
        elif destination.exists():
            try:
                if os.path.samefile(destination, source):
                    continue
            except OSError:
                pass
            raise WickedRuntimeError(
                f"refusing to replace an existing file beside the executable: {destination}")
        changes.append((destination, source))

    provenance_destination = executable.with_name(executable.name + ".provenance.json")
    if staged_provenance is not None:
        if staged_executable is None or not staged_provenance.is_file():
            raise WickedRuntimeError("staged provenance requires a staged executable and a regular sidecar")
        if (provenance_destination.exists() and not provenance_destination.is_file()
                and not provenance_destination.is_symlink()):
            raise WickedRuntimeError(f"provenance destination is not a file: {provenance_destination}")
    if not changes and staged_executable is None:
        return []
    try:
        recovery = Path(tempfile.mkdtemp(prefix=".wicked-runtime-", dir=executable.parent))
    except OSError as error:
        raise WickedRuntimeError(f"could not prepare Wicked runtime beside {executable}: {error}") from error
    changed: list[tuple[Path, Path | None]] = []
    keep_recovery = False
    try:
        prepared: list[tuple[Path, Path | None, Path | None]] = []
        for index, (destination, source) in enumerate(changes):
            backup = recovery / f"previous-{index}" if destination.is_symlink() else None
            if backup is not None:
                backup.symlink_to(os.readlink(destination))
            replacement = recovery / f"next-{index}" if source is not None else None
            if replacement is not None:
                replacement.symlink_to(source)
            prepared.append((destination, replacement, backup))
        if staged_provenance is not None:
            backup = None
            if provenance_destination.is_symlink():
                backup = recovery / "previous-provenance"
                backup.symlink_to(os.readlink(provenance_destination))
            elif provenance_destination.exists():
                backup = recovery / "previous-provenance"
                shutil.copy2(provenance_destination, backup)
            prepared.append((provenance_destination, staged_provenance, backup))
        (recovery / "recovery.json").write_text(json.dumps([
            {"destination": str(destination), "backup": str(backup) if backup is not None else None}
            for destination, _, backup in prepared], indent=2) + "\n", encoding="utf-8")
        for destination, replacement, backup in prepared:
            if replacement is None:
                destination.unlink()
            else:
                os.replace(replacement, destination)
            changed.append((destination, backup))
        if staged_executable is not None:
            os.replace(staged_executable, executable)
    except OSError as error:
        rollback_errors: list[str] = []
        for destination, backup in reversed(changed):
            try:
                if backup is None:
                    destination.unlink()
                else:
                    os.replace(backup, destination)
            except OSError as rollback_error:
                rollback_errors.append(f"{destination}: {rollback_error}")
        keep_recovery = bool(rollback_errors)
        detail = (f"; rollback failed: {'; '.join(rollback_errors)}; recovery retained at {recovery}"
            if rollback_errors else "")
        raise WickedRuntimeError(f"could not publish Wicked runtime beside {executable}: {error}{detail}") from error
    finally:
        if not keep_recovery:
            # Publication has committed; cleanup cannot turn success into a
            # reported failure after replacing the executable.
            shutil.rmtree(recovery, ignore_errors=True)
    return [destination for destination, source in changes if source is not None]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path, help="WickedEngine source directory")
    args = parser.parse_args(argv)
    try:
        staged = stage_wicked_runtime_libraries(args.executable, args.source)
    except WickedRuntimeError as error:
        print(f"wicked-runtime: {error}", file=sys.stderr)
        return 2
    if staged:
        print("Staged Wicked runtime libraries: " + ", ".join(str(path) for path in staged))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
