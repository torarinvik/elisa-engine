#!/usr/bin/env python3
"""Reject Wicked static libraries assembled from mixed libc++ ABI builds."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ABI_TAG = re.compile(r"\[abi:nq[a-z](\d+)\]")


def abi_versions(symbol_dump: str) -> set[str]:
    return set(ABI_TAG.findall(symbol_dump))


def inspect_symbols(path: Path) -> tuple[set[str], str | None]:
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


def inspect_archive(path: Path) -> tuple[set[str], str | None]:
    if not path.is_file():
        return set(), f"archive does not exist: {path}"
    return inspect_symbols(path)


def inspect_compiler(compiler: str) -> tuple[set[str], str | None]:
    resolved = shutil.which(compiler)
    if resolved is None and Path(compiler).is_file():
        resolved = str(Path(compiler).resolve())
    if resolved is None:
        return set(), f"C++ compiler was not found: {compiler}"

    source_text = """#include <string>
#include <vector>
std::string elisa_abi_probe(const std::string& value) {
    std::vector<std::string> values{value};
    return values.front();
}
"""
    with tempfile.TemporaryDirectory(prefix="elisa-cxx-abi-") as directory:
        root = Path(directory)
        source = root / "abi_probe.cpp"
        object_file = root / "abi_probe.o"
        source.write_text(source_text, encoding="utf-8")
        result = subprocess.run([resolved, "-std=c++17", "-c", str(source), "-o", str(object_file)],
            capture_output=True, text=True, check=False)
        if result.returncode != 0:
            details = result.stderr.strip() or result.stdout.strip() or f"exit status {result.returncode}"
            return set(), f"could not compile the C++ ABI probe with {resolved}: {details}"
        versions, error = inspect_symbols(object_file)
        if error:
            return set(), error
        if not versions:
            return set(), f"C++ ABI tags were not found in compiler output: {resolved}"
        return versions, None


def compiler_abi_mismatch(archive_versions: set[str], compiler_versions: set[str]) -> str | None:
    if compiler_versions == archive_versions:
        return None
    archive_text = ", ".join(sorted(archive_versions)) or "none"
    compiler_text = ", ".join(sorted(compiler_versions)) or "none"
    return (f"C++ compiler libc++ ABI tags ({compiler_text}) do not match linked archive tags "
        f"({archive_text}); select a matching compiler or rebuild all C++ archives.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="C++ compiler used to link the Elisa native bridge")
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
    if args.compiler:
        compiler_versions, error = inspect_compiler(args.compiler)
        if error:
            print(f"Wicked ABI check: {error}", file=sys.stderr)
            return 2
        mismatch = compiler_abi_mismatch(all_versions, compiler_versions)
        if mismatch:
            print(f"Wicked ABI check: {mismatch}", file=sys.stderr)
            return 1
        print(f"Wicked ABI check: compiler and all tagged archives use libc++ ABI {version}")
    else:
        print(f"Wicked ABI check: all tagged archives use libc++ ABI {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
