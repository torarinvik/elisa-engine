#!/usr/bin/env python3
"""Stage Wicked's executable-relative shader compiler libraries for dev runs."""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


RUNTIME_LIBRARIES = ("libdxcompiler.dylib", "libmetalirconverter.dylib")
REQUIRED_RUNTIME_LIBRARIES = frozenset({"libdxcompiler.dylib"})


class WickedRuntimeError(ValueError):
    """A Wicked runtime library cannot be staged beside the executable."""


def stage_wicked_runtime_libraries(executable: Path, wicked_source: Path) -> list[Path]:
    """Link Wicked runtime compiler libraries beside a development executable.

    Wicked loads DXC using an executable-relative path. Development executables
    live outside the Wicked checkout, so an rpath alone cannot satisfy that
    lookup. Symlinks keep one authoritative copy in the selected checkout.
    """
    executable = executable.expanduser().absolute()
    wicked_source = wicked_source.expanduser().resolve()
    if not executable.is_file():
        raise WickedRuntimeError(f"native executable does not exist: {executable}")
    if not wicked_source.is_dir():
        raise WickedRuntimeError(f"Wicked source directory does not exist: {wicked_source}")

    staged: list[Path] = []
    for name in RUNTIME_LIBRARIES:
        source = wicked_source / name
        if not source.is_file():
            if name in REQUIRED_RUNTIME_LIBRARIES:
                raise WickedRuntimeError(f"Wicked shader compiler is missing: {source}")
            stale_destination = executable.parent / name
            if stale_destination.is_symlink():
                stale_destination.unlink()
            elif stale_destination.exists():
                raise WickedRuntimeError(
                    f"{source.name} is absent from this Wicked checkout but a file remains beside "
                    f"the executable: {stale_destination}"
                )
            continue

        destination = executable.parent / name
        if destination.is_symlink():
            if destination.resolve() == source:
                continue
            # These exact names are reserved for Wicked's runtime libraries.
            # Repoint an older development symlink when switching checkouts.
            destination.unlink()
        elif destination.exists():
            try:
                if os.path.samefile(destination, source):
                    continue
            except OSError:
                pass
            raise WickedRuntimeError(
                f"refusing to replace an existing file beside the executable: {destination}"
            )

        try:
            destination.symlink_to(source)
        except OSError as error:
            raise WickedRuntimeError(f"could not stage {source} beside {executable}: {error}") from error
        staged.append(destination)
    return staged


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
