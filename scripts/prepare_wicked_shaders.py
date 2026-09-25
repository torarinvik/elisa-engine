#!/usr/bin/env python3
"""Compile Wicked's full Metal shader permutation set for an Elisa project.

The result is written to ``<project>/shaders/metal`` and accompanied by the
same content-verified manifest used by the macOS application packager. Shader
compilation runs in a temporary directory so it never modifies the Wicked
checkout or publishes a partial shader set when compilation fails.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from package_macos_app import PackageError, SHADER_MANIFEST_NAME, shader_manifest


ENGINE_ROOT = Path(__file__).resolve().parents[1]


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True,
        help="Elisa project that will receive the prepared shader library")
    parser.add_argument("--wicked-root", type=Path,
        default=Path(os.environ.get("WICKED_ROOT", ENGINE_ROOT.parent / "amazing-labyrinth-wickedengine")),
        help="WickedEngine checkout (or WICKED_ROOT)")
    parser.add_argument("--wicked-build", type=Path,
        help="WickedEngine build directory (or WICKED_BUILD; defaults to build-elisa-sdl3)")
    parser.add_argument("--compiler", type=Path,
        help="offlineshadercompiler executable (defaults to the selected Wicked build)")
    return parser.parse_args(argv)


def replace_manifest(shader_root: Path) -> str:
    manifest = shader_manifest(shader_root)
    destination = shader_root / SHADER_MANIFEST_NAME
    temporary = destination.with_name(destination.name + ".tmp")
    try:
        temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return str(manifest["fingerprint"])


def ensure_no_symlink_parents(path: Path, root: Path) -> None:
    current = root
    for part in path.relative_to(root).parts:
        current /= part
        if current.is_symlink():
            raise PackageError(f"prepared shader path must not traverse a symbolic link: {current}")


def prepare(project: Path, wicked_root: Path, wicked_build: Path | None,
    compiler: Path | None) -> int:
    if sys.platform != "darwin":
        print("Wicked Metal shader preparation currently requires macOS.", file=sys.stderr)
        return 2

    project = project.expanduser().resolve()
    wicked_root = wicked_root.expanduser().resolve()
    wicked_build = (wicked_build or Path(os.environ.get("WICKED_BUILD",
        wicked_root / "build-elisa-sdl3"))).expanduser().resolve()
    wicked_source = wicked_root / "WickedEngine"
    compiler = (compiler or wicked_build / "WickedEngine" / "offlineshadercompiler")
    compiler = compiler.expanduser().resolve()
    if not project.is_dir():
        print(f"Elisa project directory does not exist: {project}", file=sys.stderr)
        return 2
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        print(f"Wicked offline shader compiler is missing or not executable: {compiler}", file=sys.stderr)
        return 2
    runtime_libraries = (
        wicked_source / "libdxcompiler.dylib",
        wicked_source / "libmetalirconverter.dylib",
    )
    missing = [path for path in runtime_libraries if not path.is_file()]
    if missing:
        print("Wicked Metal shader compiler libraries are missing:\n  " +
            "\n  ".join(str(path) for path in missing), file=sys.stderr)
        return 2

    shader_root = project / "shaders"
    if shader_root.is_symlink() or (shader_root.exists() and not shader_root.is_dir()):
        print(f"Project shader path must be a real directory: {shader_root}", file=sys.stderr)
        return 2
    backend_root = shader_root / "metal"
    if backend_root.is_symlink() or (backend_root.exists() and not backend_root.is_dir()):
        print(f"Project Metal shader path must be a real directory: {backend_root}", file=sys.stderr)
        return 2

    # Wicked resolves the compiler libraries relative to its current working
    # directory and writes all listed permutations under ./shaders/metal.
    # Give it an isolated workspace and publish only successful binaries.
    with tempfile.TemporaryDirectory(prefix="elisa-wicked-shaders-") as temporary:
        workspace = Path(temporary)
        for library in runtime_libraries:
            (workspace / library.name).symlink_to(library)
        command = [str(compiler), "metal", "quiet"]
        print("Preparing Wicked Metal shader permutations...", flush=True)
        try:
            result = subprocess.run(command, cwd=workspace, check=False)
        except OSError as error:
            print(f"Could not start Wicked's offline shader compiler: {error}", file=sys.stderr)
            return 127
        if result.returncode != 0:
            print(f"Wicked shader compilation failed with status {result.returncode}; project shaders were not changed.",
                file=sys.stderr)
            return result.returncode if result.returncode > 0 else 1

        compiled_root = workspace / "shaders" / "metal"
        binaries = sorted(path for path in compiled_root.rglob("*.cso") if path.is_file())
        if not binaries:
            print("Wicked reported success without producing Metal shader binaries.", file=sys.stderr)
            return 1

        try:
            backend_root.mkdir(parents=True, exist_ok=True)
            manifest_path = shader_root / SHADER_MANIFEST_NAME
            manifest_path.unlink(missing_ok=True)
            for source in binaries:
                relative = source.relative_to(compiled_root)
                destination = backend_root / relative
                ensure_no_symlink_parents(destination.parent, backend_root)
                destination.parent.mkdir(parents=True, exist_ok=True)
                temporary_binary = destination.with_name(destination.name + ".tmp")
                try:
                    shutil.copyfile(source, temporary_binary)
                    os.replace(temporary_binary, destination)
                finally:
                    temporary_binary.unlink(missing_ok=True)
            fingerprint = replace_manifest(shader_root)
        except (OSError, PackageError) as error:
            (shader_root / SHADER_MANIFEST_NAME).unlink(missing_ok=True)
            print(f"Could not publish the prepared shader library: {error}", file=sys.stderr)
            return 1

    print(f"Prepared {len(binaries)} Metal shader permutations in {backend_root}")
    print(f"Shader manifest fingerprint: {fingerprint}")
    return 0


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    return prepare(args.project, args.wicked_root, args.wicked_build, args.compiler)


if __name__ == "__main__":
    raise SystemExit(main())
