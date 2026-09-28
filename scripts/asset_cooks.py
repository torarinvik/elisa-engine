#!/usr/bin/env python3
"""Validate and order a project's declared asset cooks for the build runner.

Each `asset_cooks` entry in `elisa.project.json` names an importer, its
inputs and its output inside the project. This module turns those entries
into cooker commands and rejects anything that escapes the project or
mismatches the importer, so the runner itself stays small.
"""

from __future__ import annotations

import ast
import importlib.metadata
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shlex
import sys
import time
import uuid
from typing import Callable
from urllib.parse import unquote, urlsplit

from cook_cache import (atomic_write_json, compiler_identity, content_fingerprint,
    local_source_closure, sha256_file)

ENGINE_ROOT = Path(__file__).resolve().parents[1]
CACHE_FORMAT_VERSION = 1
GLB_JSON_CHUNK = 0x4E4F534A
_VERSION_CACHE: dict[str, dict[str, str]] = {}


class BuildConfigurationError(Exception):
    pass


def declared_project_path(project: Path, value: object, label: str, *, must_exist: bool) -> Path:
    if not isinstance(value, str) or not value or "\0" in value or "\\" in value:
        raise BuildConfigurationError(f"asset cook {label} must be a safe project-relative path")
    logical = PurePosixPath(value)
    if logical.is_absolute() or any(part in ("", ".", "..") for part in value.split("/")):
        raise BuildConfigurationError(f"asset cook {label} must be a safe project-relative path")
    try:
        path = (project / Path(*logical.parts)).resolve(strict=must_exist)
    except OSError as error:
        raise BuildConfigurationError(f"asset cook {label} cannot be resolved: {error}") from error
    if not path.is_relative_to(project):
        raise BuildConfigurationError(f"asset cook {label} resolves outside the project")
    if must_exist and not path.is_file():
        raise BuildConfigurationError(f"asset cook {label} is not a regular file: {path}")
    return path


def declared_textures(project: Path, declaration: dict[str, object], index: int,
    importer: object, output: Path) -> list[tuple[str, Path]]:
    """Validate an asset cook's image sections, returned in section-name order."""
    textures = declaration.get("textures", {})
    if not isinstance(textures, dict) or len(textures) > 16:
        raise BuildConfigurationError(f"asset_cooks[{index}].textures must be an object of at most 16 sections")
    if textures and (importer not in ("gltf", "images") or output.suffix.lower() != ".elpk"):
        raise BuildConfigurationError(
            f"asset_cooks[{index}].textures requires the gltf or images importer and an .elpk output")
    declared = []
    for section in sorted(textures):
        if (not 1 <= len(section) <= 15 or section in ("mesh", "manifest") or
                any(not ("a" <= character <= "z" or "0" <= character <= "9" or character == "_")
                    for character in section)):
            raise BuildConfigurationError(f"asset_cooks[{index}].textures section {section!r} is not a safe name")
        declared.append((section, declared_project_path(project, textures[section],
            f"asset_cooks[{index}].textures.{section}", must_exist=True)))
    return declared


def declared_dependencies(project: Path, declaration: dict[str, object], index: int,
    output: Path) -> list[tuple[Path, str]]:
    """Validate an asset cook's bundle dependencies.

    Each entry is a project path and the name the manifest records for it,
    which is relative to the output bundle's directory.
    """
    dependencies = declaration.get("dependencies", [])
    if not isinstance(dependencies, list) or len(dependencies) > 16:
        raise BuildConfigurationError(f"asset_cooks[{index}].dependencies must be an array of at most 16 bundles")
    if dependencies and output.suffix.lower() != ".elpk":
        raise BuildConfigurationError(f"asset_cooks[{index}].dependencies requires an .elpk output")
    declared = []
    for position, value in enumerate(dependencies):
        label = f"asset_cooks[{index}].dependencies[{position}]"
        path = declared_project_path(project, value, label, must_exist=False)
        if path == output or path.suffix.lower() != ".elpk":
            raise BuildConfigurationError(f"asset cook {label} must name another .elpk bundle")
        if not path.is_relative_to(output.parent):
            raise BuildConfigurationError(
                f"asset cook {label} must be in the output bundle's directory or below it")
        declared.append((path, path.relative_to(output.parent).as_posix()))
    if len({path for path, _ in declared}) != len(declared):
        raise BuildConfigurationError(f"asset_cooks[{index}].dependencies must not repeat a bundle")
    return declared


def image_size_bound(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 1 <= value <= 8192:
        raise BuildConfigurationError(f"{label} must be an integer from 1 to 8192")
    return value


def declared_image_cook(project: Path, declaration: dict[str, object], index: int, output: Path,
    textures: list[tuple[str, Path]], dependencies: list[tuple[Path, str]]) -> tuple[Path, int]:
    """Validate a single-image cook that bounds a texture's size for the runtime."""
    for key in ("asset_path", "max_triangles", "animation_source", "texture_output", "texture_max_size"):
        if key in declaration:
            raise BuildConfigurationError(f"asset_cooks[{index}] image importer does not accept {key}")
    if textures or dependencies:
        raise BuildConfigurationError(f"asset_cooks[{index}] image importer takes one source, not sections")
    source = declared_project_path(project, declaration.get("source"), f"asset_cooks[{index}].source", must_exist=True)
    if source.suffix.lower() not in (".png", ".jpg", ".jpeg") or output.suffix.lower() != source.suffix.lower():
        raise BuildConfigurationError(f"asset_cooks[{index}] image importer needs a .png or .jpg source and output of the same type")
    if output == source:
        raise BuildConfigurationError(f"asset_cooks[{index}] cannot overwrite its source")
    return source, image_size_bound(declaration.get("max_size"), f"asset_cooks[{index}].max_size")


def asset_cook_command(project: Path, declaration: object,
    index: int) -> tuple[Path, str, list[str], list[Path]]:
    """Validate one asset cook, returning its output, label, command and dependency outputs."""
    if not isinstance(declaration, dict):
        raise BuildConfigurationError(f"asset_cooks[{index}] must be an object")
    output = declared_project_path(project, declaration.get("output"), f"asset_cooks[{index}].output", must_exist=False)
    importer = declaration.get("importer", "fbx")
    dependencies = declared_dependencies(project, declaration, index, output)
    textures = declared_textures(project, declaration, index, importer, output)
    if importer == "images":
        for key in ("source", "asset_path", "max_triangles"):
            if key in declaration:
                raise BuildConfigurationError(f"asset_cooks[{index}] images importer does not accept {key}")
        if not textures:
            raise BuildConfigurationError(f"asset_cooks[{index}] images importer requires textures")
        command = [sys.executable, str(ENGINE_ROOT / "scripts/cook_image_bundle.py"), "--output", str(output)]
        source_label = "images"
    elif importer == "image":
        source, max_size = declared_image_cook(project, declaration, index, output, textures, dependencies)
        command = [sys.executable, str(ENGINE_ROOT / "scripts/cook_image_asset.py"), str(source),
            "--output", str(output), "--max-size", str(max_size)]
        source_label = str(source.relative_to(project))
    else:
        source = declared_project_path(project, declaration.get("source"), f"asset_cooks[{index}].source", must_exist=True)
        if importer == "fbx":
            cooker = ENGINE_ROOT / "scripts/cook_fbx_asset.py"
        elif importer == "gltf":
            if source.suffix.lower() != ".gltf":
                raise BuildConfigurationError(f"asset_cooks[{index}] gltf importer requires a .gltf source")
            cooker = ENGINE_ROOT / "scripts/cook_gltf_asset.py"
        elif importer == "glb":
            if source.suffix.lower() != ".glb":
                raise BuildConfigurationError(f"asset_cooks[{index}] glb importer requires a .glb source")
            cooker = ENGINE_ROOT / "scripts/cook_glb_asset.py"
        else:
            raise BuildConfigurationError(
                f"asset_cooks[{index}].importer must be 'fbx', 'gltf', 'glb', 'image' or 'images'")
        asset_path = declaration.get("asset_path")
        if not isinstance(asset_path, str) or not asset_path:
            raise BuildConfigurationError(f"asset_cooks[{index}].asset_path must be a non-empty package identity")
        if "\0" in asset_path or "\\" in asset_path or PurePosixPath(asset_path).is_absolute() or \
                any(part in ("", ".", "..") for part in asset_path.split("/")):
            raise BuildConfigurationError(f"asset_cooks[{index}].asset_path must be a safe relative identity")
        if output == source:
            raise BuildConfigurationError(f"asset_cooks[{index}] cannot overwrite its source")
        max_triangles = declaration.get("max_triangles")
        if max_triangles is not None and (isinstance(max_triangles, bool) or
            not isinstance(max_triangles, int) or not 1 <= max_triangles <= 1000000):
            raise BuildConfigurationError(f"asset_cooks[{index}].max_triangles must be an integer in [1, 1000000]")
        if importer == "gltf" and max_triangles is not None:
            raise BuildConfigurationError(f"asset_cooks[{index}] gltf importer does not accept max_triangles")
        ignore_material_textures = declaration.get("ignore_material_textures", False)
        if not isinstance(ignore_material_textures, bool):
            raise BuildConfigurationError(
                f"asset_cooks[{index}].ignore_material_textures must be a boolean")
        if ignore_material_textures and importer != "fbx":
            raise BuildConfigurationError(
                f"asset_cooks[{index}].ignore_material_textures requires the fbx importer")
        all_meshes = declaration.get("all_meshes", False)
        if not isinstance(all_meshes, bool):
            raise BuildConfigurationError(f"asset_cooks[{index}].all_meshes must be a boolean")
        if all_meshes and importer != "fbx":
            raise BuildConfigurationError(f"asset_cooks[{index}].all_meshes requires the fbx importer")
        command = [sys.executable, str(cooker), str(source), "--asset-path", asset_path,
            "--output", str(output)]
        if ignore_material_textures:
            command.append("--ignore-material-textures")
        if all_meshes:
            command.append("--all-meshes")
        if max_triangles is not None:
            command.extend(["--max-triangles", str(max_triangles)])
        animation_source_value = declaration.get("animation_source")
        if animation_source_value is not None:
            if importer != "glb":
                raise BuildConfigurationError(f"asset_cooks[{index}].animation_source requires the glb importer")
            animation_source = declared_project_path(project, animation_source_value,
                f"asset_cooks[{index}].animation_source", must_exist=True)
            if animation_source.suffix.lower() != ".fbx":
                raise BuildConfigurationError(f"asset_cooks[{index}].animation_source must be an .fbx source")
            command.extend(["--animation-source", str(animation_source)])
        texture_output_value = declaration.get("texture_output")
        if texture_output_value is not None:
            if importer != "glb":
                raise BuildConfigurationError(f"asset_cooks[{index}].texture_output requires the glb importer")
            texture_output = declared_project_path(project, texture_output_value,
                f"asset_cooks[{index}].texture_output", must_exist=False)
            if texture_output in (source, output):
                raise BuildConfigurationError(f"asset_cooks[{index}].texture_output cannot overwrite its source or package")
            command.extend(["--texture-output", str(texture_output)])
        texture_max_size = declaration.get("texture_max_size")
        if texture_max_size is not None:
            if texture_output_value is None:
                raise BuildConfigurationError(f"asset_cooks[{index}].texture_max_size requires texture_output")
            command.extend(["--texture-max-size", str(image_size_bound(texture_max_size,
                f"asset_cooks[{index}].texture_max_size"))])
        source_label = str(source.relative_to(project))
    for section, texture in textures:
        command.extend(["--texture", f"{section}={texture}"])
    for _, name in dependencies:
        command.extend(["--dependency", name])
    label = f"{source_label} -> {output.relative_to(project)}"
    return output, label, command, [path for path, _ in dependencies]


def asset_cook_order(project: Path, outputs: list[Path], dependencies: list[list[Path]]) -> list[int]:
    """Order cooks so each bundle is written after the bundles it depends on."""
    producers = {output: index for index, output in enumerate(outputs)}
    if len(producers) != len(outputs):
        raise BuildConfigurationError("two asset cooks write the same output")
    for index, needed in enumerate(dependencies):
        for dependency in needed:
            if dependency not in producers:
                raise BuildConfigurationError(
                    f"asset_cooks[{index}] depends on {dependency.relative_to(project)}, which no asset cook writes")
    order: list[int] = []
    state: dict[int, str] = {}

    def visit(index: int) -> None:
        if state.get(index) == "done":
            return
        if state.get(index) == "visiting":
            raise BuildConfigurationError(f"asset_cooks[{index}] is part of a dependency cycle")
        state[index] = "visiting"
        for dependency in dependencies[index]:
            visit(producers[dependency])
        state[index] = "done"
        order.append(index)

    for index in range(len(outputs)):
        visit(index)
    return order


def _declared_outputs(project: Path, declaration: dict[str, object], index: int) -> list[Path]:
    outputs = [declared_project_path(project, declaration.get("output"),
        f"asset_cooks[{index}].output", must_exist=False)]
    texture_output = declaration.get("texture_output")
    if texture_output is not None:
        outputs.append(declared_project_path(project, texture_output,
            f"asset_cooks[{index}].texture_output", must_exist=False))
    return outputs


def _read_gltf_document(source: Path, importer: str) -> dict[str, object] | None:
    try:
        if importer == "gltf":
            value = json.loads(source.read_text(encoding="utf-8"))
        else:
            with source.open("rb") as stream:
                header = stream.read(12)
                if len(header) != 12 or header[:4] != b"glTF":
                    return None
                file_size = source.stat().st_size
                offset = 12
                value = None
                while offset + 8 <= file_size:
                    chunk_header = stream.read(8)
                    if len(chunk_header) != 8:
                        return None
                    length = int.from_bytes(chunk_header[:4], "little")
                    chunk_type = int.from_bytes(chunk_header[4:], "little")
                    offset += 8
                    if length > file_size - offset:
                        return None
                    if chunk_type == GLB_JSON_CHUNK:
                        payload = stream.read(length)
                        value = json.loads(payload.rstrip(b"\0 \t\r\n").decode("utf-8"))
                        break
                    stream.seek(length, os.SEEK_CUR)
                    offset += length
        return value if isinstance(value, dict) else None
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
        # The cooker will give the detailed malformed-source error. The source
        # bytes still participate in the fingerprint, so malformed edits miss.
        return None


def _referenced_project_files(project: Path, source: Path, importer: str) -> list[Path]:
    if importer not in ("gltf", "glb"):
        return []
    document = _read_gltf_document(source, importer)
    if document is None:
        return []
    found: set[Path] = set()
    for section in ("buffers", "images"):
        resources = document.get(section, [])
        if not isinstance(resources, list):
            continue
        for resource in resources:
            if not isinstance(resource, dict):
                continue
            uri = resource.get("uri")
            if not isinstance(uri, str) or uri.startswith("data:"):
                continue
            parsed = urlsplit(uri)
            if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment or not parsed.path:
                raise BuildConfigurationError(
                    f"{source.relative_to(project)} contains a non-local {section} URI")
            path = (source.parent / unquote(parsed.path)).resolve()
            if not path.is_relative_to(project):
                raise BuildConfigurationError(
                    f"{source.relative_to(project)} references a file outside the project")
            if not path.is_file():
                raise BuildConfigurationError(
                    f"{source.relative_to(project)} references a missing file: {path.relative_to(project)}")
            found.add(path)
    return sorted(found)


def _cook_inputs(project: Path, declaration: dict[str, object], index: int,
    dependencies: list[Path]) -> list[Path]:
    importer = declaration.get("importer", "fbx")
    inputs: set[Path] = set(dependencies)
    source_value = declaration.get("source")
    if isinstance(source_value, str):
        source = declared_project_path(project, source_value,
            f"asset_cooks[{index}].source", must_exist=True)
        inputs.add(source)
        inputs.update(_referenced_project_files(project, source, str(importer)))
    animation_source = declaration.get("animation_source")
    if animation_source is not None:
        inputs.add(declared_project_path(project, animation_source,
            f"asset_cooks[{index}].animation_source", must_exist=True))
    textures = declaration.get("textures", {})
    if isinstance(textures, dict):
        for section, value in textures.items():
            inputs.add(declared_project_path(project, value,
                f"asset_cooks[{index}].textures.{section}", must_exist=True))
    return sorted(inputs)


def _python_tool_closure(cooker: Path, importer: str) -> list[Path]:
    scripts = ENGINE_ROOT / "scripts"
    pending = [cooker]
    if importer == "glb":
        # These are launched as subprocesses, so AST imports in the outer
        # cooker cannot discover them.
        pending.extend((scripts / "cook_glb_asset_blender.py", scripts / "cook_fbx_asset.py"))
    visited: set[Path] = set()
    while pending:
        source = pending.pop().resolve()
        if source in visited:
            continue
        if not source.is_file() or not source.is_relative_to(scripts.resolve()):
            raise BuildConfigurationError(f"asset cooker script is missing or outside scripts/: {source}")
        visited.add(source)
        try:
            tree = ast.parse(source.read_text(encoding="utf-8"), filename=str(source))
        except (OSError, UnicodeError, SyntaxError) as error:
            raise BuildConfigurationError(f"cannot identify asset cooker {source.name}: {error}") from error
        for node in ast.walk(tree):
            module_names: list[str] = []
            if isinstance(node, ast.Import):
                module_names.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                module_names.append(node.module)
            for name in module_names:
                module = name.lstrip(".")
                if not module:
                    continue
                candidate = scripts.joinpath(*module.split(".")).with_suffix(".py")
                if candidate.is_file() and candidate not in visited:
                    pending.append(candidate)
    return sorted(visited)


def _external_tool_identity(importer: str, declaration: dict[str, object]) -> dict[str, object]:
    identity: dict[str, object] = {
        "python": str(Path(sys.executable).resolve()),
        "python_version": sys.version,
        "platform": platform.platform(),
    }
    needs_pillow = importer == "image" or (
        importer == "glb" and declaration.get("texture_max_size") is not None)
    if needs_pillow:
        try:
            identity["pillow"] = importlib.metadata.version("Pillow")
        except importlib.metadata.PackageNotFoundError:
            identity["pillow"] = "not-installed"
    if importer in ("fbx", "glb"):
        for variable, default in (("CC", "cc"), ("CXX", "c++")):
            command = os.environ.get(variable, default)
            if command not in _VERSION_CACHE:
                _VERSION_CACHE[command] = compiler_identity(command)
            identity[variable] = _VERSION_CACHE[command]
    if importer == "glb":
        from cook_glb_asset import blender_executable
        command = shlex.quote(str(Path(blender_executable()).resolve()))
        if command not in _VERSION_CACHE:
            _VERSION_CACHE[command] = compiler_identity(command)
        identity["blender"] = _VERSION_CACHE[command]
    return identity


def _cook_fingerprint(project: Path, declaration: dict[str, object], index: int,
    cook: tuple[Path, str, list[str], list[Path]]) -> str:
    output, _, command, dependencies = cook
    importer = str(declaration.get("importer", "fbx"))
    cooker = Path(command[1]).resolve()
    tool_files = _python_tool_closure(cooker, importer)
    if importer in ("fbx", "glb"):
        dependency = ENGINE_ROOT / "dependencies/ufbx"
        meshoptimizer = ENGINE_ROOT / "dependencies/meshoptimizer"
        mikktspace = ENGINE_ROOT / "dependencies/mikktspace"
        tool_files.extend(local_source_closure(
            [dependency / "ufbx.c", mikktspace / "mikktspace.c",
                ENGINE_ROOT / "native/fbx_asset_cooker.cpp",
                meshoptimizer / "simplifier.cpp", meshoptimizer / "vcacheoptimizer.cpp",
                meshoptimizer / "indexanalyzer.cpp", meshoptimizer / "vfetchoptimizer.cpp",
                meshoptimizer / "indexgenerator.cpp", meshoptimizer / "allocator.cpp"],
            [dependency, meshoptimizer, mikktspace, ENGINE_ROOT / "native"]))
    tool_files.extend((Path(__file__).resolve(), ENGINE_ROOT / "scripts/cook_cache.py"))
    tool_files = sorted(set(tool_files))
    tools = [{"path": path.relative_to(ENGINE_ROOT).as_posix(), "sha256": sha256_file(path)}
        for path in tool_files]
    source_inputs = [{"path": path.relative_to(project).as_posix(), "sha256": sha256_file(path)}
        for path in _cook_inputs(project, declaration, index, dependencies)]
    identity = {
        "cache_format": CACHE_FORMAT_VERSION,
        "declaration": declaration,
        "outputs": [path.relative_to(project).as_posix()
            for path in _declared_outputs(project, declaration, index)],
        "inputs": source_inputs,
        "tools": tools,
        "toolchain": _external_tool_identity(importer, declaration),
    }
    return content_fingerprint(identity)


def _read_cache(path: Path) -> dict[str, object]:
    if not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        print(f"Asset cook cache miss: ignoring unreadable cache {path}", flush=True)
        return {}
    if not isinstance(value, dict) or value.get("format") != CACHE_FORMAT_VERSION:
        print("Asset cook cache miss: cache format changed", flush=True)
        return {}
    entries = value.get("entries", {})
    return entries if isinstance(entries, dict) else {}


def _cache_hit(project: Path, entry: object, fingerprint: str,
    expected_outputs: list[Path]) -> tuple[bool, str]:
    if not isinstance(entry, dict):
        return False, "no previous cook record"
    if entry.get("fingerprint") != fingerprint:
        return False, "source, options, dependency or tool identity changed"
    outputs = entry.get("outputs")
    if not isinstance(outputs, list) or not outputs:
        return False, "output record is missing"
    recorded_paths = [record.get("path") if isinstance(record, dict) else None for record in outputs]
    expected_paths = [path.relative_to(project).as_posix() for path in expected_outputs]
    if recorded_paths != expected_paths:
        return False, "output record does not match declared outputs"
    for record in outputs:
        if not isinstance(record, dict):
            return False, "output record is invalid"
        relative = record.get("path")
        expected_hash = record.get("sha256")
        if not isinstance(relative, str) or not isinstance(expected_hash, str):
            return False, "output record is invalid"
        path = (project / relative).resolve()
        if not path.is_relative_to(project) or not path.is_file():
            return False, f"generated output is missing: {relative}"
        try:
            if sha256_file(path) != expected_hash:
                return False, f"generated output changed: {relative}"
        except OSError:
            return False, f"generated output cannot be read: {relative}"
    return True, ""


def _staged_outputs(outputs: list[Path]) -> dict[Path, Path]:
    staged: dict[Path, Path] = {}
    for output in outputs:
        output.parent.mkdir(parents=True, exist_ok=True)
        staged[output] = output.with_name(
            f".{output.stem}.elisa-cook-{uuid.uuid4().hex}{output.suffix}")
    return staged


def cook_declared_assets(project: Path, config: dict[str, object], run: Callable[..., int],
    *, force: bool = False) -> int:
    project = project.expanduser().resolve()
    declarations = config.get("asset_cooks", [])
    if not isinstance(declarations, list) or len(declarations) > 64:
        raise BuildConfigurationError("project 'asset_cooks' must be an array of at most 64 entries")
    if not declarations:
        return 0
    cooks = [asset_cook_command(project, declaration, index) for index, declaration in enumerate(declarations)]
    all_outputs = [path for index, declaration in enumerate(declarations)
        for path in _declared_outputs(project, declaration, index)]
    if len(set(all_outputs)) != len(all_outputs):
        raise BuildConfigurationError("asset cooks must not publish to the same output path")
    source_inputs: set[Path] = set()
    for index, declaration in enumerate(declarations):
        assert isinstance(declaration, dict)
        for field in ("source", "animation_source"):
            value = declaration.get(field)
            if value is not None:
                source_inputs.add(declared_project_path(project, value,
                    f"asset_cooks[{index}].{field}", must_exist=True))
        textures = declaration.get("textures", {})
        if isinstance(textures, dict):
            for section, value in textures.items():
                source_inputs.add(declared_project_path(project, value,
                    f"asset_cooks[{index}].textures.{section}", must_exist=True))
    if set(all_outputs) & source_inputs:
        raise BuildConfigurationError("an asset cook output cannot overwrite a declared source or texture")
    cache_path = project / "build/.elisa-asset-cook-cache.json"
    entries = _read_cache(cache_path)
    cook_seconds = 0.0
    cache_hits = 0
    cache_misses = 0
    for index in asset_cook_order(project, [cook[0] for cook in cooks], [cook[3] for cook in cooks]):
        output, label, command, _ = cooks[index]
        declaration = declarations[index]
        assert isinstance(declaration, dict)
        entry_key = output.relative_to(project).as_posix()
        fingerprint = _cook_fingerprint(project, declaration, index, cooks[index])
        outputs = _declared_outputs(project, declaration, index)
        hit, reason = _cache_hit(project, entries.get(entry_key), fingerprint, outputs)
        if hit and not force:
            cache_hits += 1
            print(f"Asset cook cache hit: {label}", flush=True)
            continue
        cache_misses += 1
        if force:
            reason = "forced recook"
        print(f"Asset cook cache miss ({reason}): {label}", flush=True)
        staged = _staged_outputs(outputs)
        replacements = {str(final): str(temporary) for final, temporary in staged.items()}
        staged_command = [replacements.get(argument, argument) for argument in command]
        started = time.perf_counter()
        try:
            status = run(staged_command, cwd=project)
        except BaseException:
            for temporary in staged.values():
                temporary.unlink(missing_ok=True)
            raise
        cook_seconds += time.perf_counter() - started
        if status != 0:
            for temporary in staged.values():
                temporary.unlink(missing_ok=True)
            return status
        output_records = []
        for final, temporary in staged.items():
            if not temporary.is_file() or temporary.stat().st_size == 0:
                for path in staged.values():
                    path.unlink(missing_ok=True)
                raise BuildConfigurationError(f"asset cooker did not produce a valid output: {final}")
        try:
            for final, temporary in staged.items():
                os.replace(temporary, final)
                output_records.append({"path": final.relative_to(project).as_posix(),
                    "sha256": sha256_file(final)})
        finally:
            for temporary in staged.values():
                temporary.unlink(missing_ok=True)
        entries[entry_key] = {"fingerprint": fingerprint, "outputs": output_records}
        atomic_write_json(cache_path, {"format": CACHE_FORMAT_VERSION, "entries": entries})
    print(f"Asset cooks: {cache_hits} cache hit(s), {cache_misses} cook(s), "
        f"{cook_seconds:.2f}s cooker time", flush=True)
    return 0
