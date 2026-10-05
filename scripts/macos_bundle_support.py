"""Bundle support for package_macos_app: resource filters, the compiled
shader manifest, and Mach-O dynamic library bundling.

bundle_dynamic_libraries stages the non-system dylib closure of an
executable into a bundle's Frameworks directory and rewrites load commands
to @rpath.
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import stat
import subprocess
from pathlib import Path


class PackageError(ValueError):
    """The project or release bundle is not packageable."""


def executable_provenance(executable: Path) -> dict[str, object] | None:
    """Read build metadata and reject a sidecar for different executable bytes."""
    sidecar = executable.with_name(executable.name + ".provenance.json")
    if not sidecar.is_file():
        return None
    try:
        record = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError(f"could not read build provenance {sidecar}: {error}") from error
    if not isinstance(record, dict):
        raise PackageError(f"build provenance must contain an object: {sidecar}")
    binary = record.get("binary")
    if isinstance(binary, dict) and "sha256" in binary:
        expected = binary["sha256"]
        actual = hashlib.sha256(executable.read_bytes()).hexdigest()
        if not isinstance(expected, str) or re.fullmatch(r"[0-9a-fA-F]{64}", expected) is None:
            raise PackageError(f"build provenance has an invalid binary sha256: {sidecar}")
        if expected.lower() != actual:
            raise PackageError(f"build provenance binary sha256 does not match executable: {sidecar}")
    return record


IGNORED_NAMES = frozenset({".git", ".gitattributes", ".gitignore", ".DS_Store",
    "__pycache__", "Thumbs.db"})
MAX_RESOURCE_ENTRIES = 256


SHADER_METADATA_SUFFIX = ".wishadermeta"
SHADER_MANIFEST_NAME = "elisa.shader-manifest.json"
SHADER_GENERATED_INVENTORY_NAME = "elisa.metal-generated.json"
SHADER_MANIFEST_SCHEMA = 2
SHADER_BINARY_SUFFIXES = frozenset({".cso", ".spv"})
SHADER_BACKENDS = frozenset({"hlsl6", "metal", "spirv"})


def ignore_litter(_directory: str, names: list[str]) -> set[str]:
    return {name for name in names if name in IGNORED_NAMES}


def ignore_shader_metadata(directory: str, names: list[str]) -> set[str]:
    # Shader metadata records absolute source dependency paths from the build
    # machine. Wicked treats a compiled shader without metadata as up to date,
    # which is exactly what a relocated bundle without the source tree needs.
    return ignore_litter(directory, names) | {
        name for name in names if name.endswith(SHADER_METADATA_SUFFIX)
        or name == SHADER_GENERATED_INVENTORY_NAME}


def shader_manifest(shader_root: Path) -> dict[str, object]:
    """Describe compiled shader inputs and their backends with a stable fingerprint."""
    files: list[dict[str, object]] = []
    backends: set[str] = set()
    for path in sorted(shader_root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SHADER_BINARY_SUFFIXES:
            continue
        relative = path.relative_to(shader_root).as_posix()
        backend = relative.partition("/")[0]
        if backend not in SHADER_BACKENDS or "/" not in relative:
            raise PackageError(f"compiled shader is outside a known backend directory: {relative}")
        backends.add(backend)
        files.append({
            "path": relative,
            "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })
    if not files:
        raise PackageError(f"shader directory has no compiled shader binaries: {shader_root}")
    payload: dict[str, object] = {
        "schema": SHADER_MANIFEST_SCHEMA,
        "backends": sorted(backends),
        "files": files,
    }
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    payload["fingerprint"] = hashlib.sha256(canonical).hexdigest()
    return payload


MACH_O_MAGICS = (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe")
SYSTEM_LIBRARY_PREFIXES = ("/usr/lib/", "/System/")
MAX_BUNDLED_LIBRARIES = 64


def is_mach_o(path: Path) -> bool:
    with path.open("rb") as stream:
        return stream.read(4) in MACH_O_MAGICS


def run_tool(*command: str) -> str:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise PackageError(f"{command[0]} failed: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def linked_libraries(file: Path) -> list[str]:
    """Return the install names `file` loads, excluding its own identity."""
    names: list[str] = []
    for line in run_tool("otool", "-L", str(file)).splitlines()[1:]:
        entry = line.strip().split(" (compatibility", 1)[0]
        if entry and Path(entry).name != file.name:
            names.append(entry)
    return names


def existing_rpaths(file: Path) -> set[str]:
    paths: set[str] = set()
    lines = run_tool("otool", "-l", str(file)).splitlines()
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH" and index + 2 < len(lines):
            paths.add(lines[index + 2].strip().split(" ", 2)[1])
    return paths


def bundle_dynamic_libraries(binary: Path, frameworks: Path) -> list[str]:
    """Copy every non-system dynamic library `binary` loads, transitively,
    into `frameworks`, point each load command at @rpath, and re-sign.

    Development builds link Homebrew libraries by absolute path. A bundle
    must not depend on that prefix, so the closure is staged beside the
    executable and found through a loader-relative run path instead.
    """
    binary.chmod(binary.stat().st_mode | stat.S_IWUSR)
    staged: dict[str, Path] = {}
    pending = [binary]
    while pending:
        file = pending.pop()
        for install_name in linked_libraries(file):
            if install_name.startswith(SYSTEM_LIBRARY_PREFIXES) or install_name.startswith("@"):
                continue
            name = Path(install_name).name
            if name not in staged:
                if len(staged) >= MAX_BUNDLED_LIBRARIES:
                    raise PackageError(f"more than {MAX_BUNDLED_LIBRARIES} dynamic libraries to bundle")
                source = Path(install_name)
                if not source.is_file():
                    raise PackageError(f"linked library is missing: {install_name}")
                frameworks.mkdir(parents=True, exist_ok=True)
                destination = frameworks / name
                shutil.copy2(source, destination, follow_symlinks=True)
                destination.chmod(destination.stat().st_mode | stat.S_IWUSR)
                run_tool("install_name_tool", "-id", f"@rpath/{name}", str(destination))
                staged[name] = destination
                pending.append(destination)
            run_tool("install_name_tool", "-change", install_name, f"@rpath/{name}", str(file))
    for file, rpath in [(binary, "@loader_path/../Frameworks")] + [
            (library, "@loader_path") for library in staged.values()]:
        if staged and rpath not in existing_rpaths(file):
            run_tool("install_name_tool", "-add_rpath", rpath, str(file))
        # Editing load commands invalidates the signature; an ad-hoc one keeps
        # the loader happy on Apple silicon until a release identity signs.
        run_tool("codesign", "--force", "--sign", "-", str(file))
    return sorted(staged)
