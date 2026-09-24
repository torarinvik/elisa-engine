#!/usr/bin/env python3
"""Reject Wicked static libraries assembled from mixed libc++ ABI builds."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


ABI_TAG = re.compile(r"\[abi:nq[a-z](\d+)\]")


def abi_versions(symbol_dump: str) -> set[str]:
    return set(ABI_TAG.findall(symbol_dump))


def inspect_archive(path: Path) -> tuple[set[str], str | None]:
    if not path.is_file():
        return set(), f"archive does not exist: {path}"

    commands = []
    if sys.platform == "darwin" and shutil.which("xcrun"):
        commands.append(["xcrun", "llvm-nm", "-A", "--demangle", str(path)])
    if shutil.which("llvm-nm"):
        commands.append(["llvm-nm", "-A", "--demangle", str(path)])
    if shutil.which("nm"):
        commands.append(["nm", "-A", "-C", str(path)])
    if not commands:
        return set(), "could not find llvm-nm or nm to inspect static archives"

    errors = []
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if result.returncode == 0:
            return abi_versions(result.stdout), None
        errors.append(result.stderr.strip() or result.stdout.strip())
    return set(), f"could not inspect {path}: {next((error for error in errors if error), 'nm failed')}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", nargs="+", type=Path)
    args = parser.parse_args()

    per_archive: dict[Path, set[str]] = {}
    errors = []
    for path in args.archives:
        versions, error = inspect_archive(path)
        per_archive[path] = versions
        if error:
            errors.append(error)

    if errors:
        for error in errors:
            print(f"Wicked ABI check: {error}", file=sys.stderr)
        return 2

    all_versions = set().union(*per_archive.values())
    if len(all_versions) > 1:
        print("Wicked ABI check: linked archives contain mixed libc++ ABI tags:", file=sys.stderr)
        for path, versions in per_archive.items():
            printable = ", ".join(sorted(versions)) if versions else "no tagged libc++ symbols"
            print(f"  {path}: {printable}", file=sys.stderr)
        print("Rebuild every C++ archive and the Elisa native bridge with one toolchain.", file=sys.stderr)
        return 1

    version = next(iter(all_versions), None)
    if version is None:
        print("Wicked ABI check: no libc++ ABI tags found; compatibility could not be verified", file=sys.stderr)
        return 2
    print(f"Wicked ABI check: all tagged archives use libc++ ABI {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
