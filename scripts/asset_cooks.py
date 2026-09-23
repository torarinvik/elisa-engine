#!/usr/bin/env python3
"""Validate and order a project's declared asset cooks for the build runner.

Each `asset_cooks` entry in `elisa.project.json` names an importer, its
inputs and its output inside the project. This module turns those entries
into cooker commands and rejects anything that escapes the project or
mismatches the importer, so the runner itself stays small.
"""

from __future__ import annotations

from pathlib import Path, PurePosixPath
import sys
from typing import Callable

ENGINE_ROOT = Path(__file__).resolve().parents[1]


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
        command = [sys.executable, str(cooker), str(source), "--asset-path", asset_path,
            "--output", str(output)]
        if ignore_material_textures:
            command.append("--ignore-material-textures")
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


def cook_declared_assets(project: Path, config: dict[str, object], run: Callable[..., int]) -> int:
    declarations = config.get("asset_cooks", [])
    if not isinstance(declarations, list) or len(declarations) > 64:
        raise BuildConfigurationError("project 'asset_cooks' must be an array of at most 64 entries")
    cooks = [asset_cook_command(project, declaration, index) for index, declaration in enumerate(declarations)]
    for index in asset_cook_order(project, [cook[0] for cook in cooks], [cook[3] for cook in cooks]):
        _, label, command, _ = cooks[index]
        print(f"Cooking project asset: {label}", flush=True)
        status = run(command, cwd=project)
        if status != 0:
            return status
    return 0
