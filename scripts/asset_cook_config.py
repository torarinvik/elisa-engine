"""Validate project paths and asset-cook declarations.

Kept separate from cook orchestration so configuration rules stay easy to audit.
"""

from __future__ import annotations

from pathlib import Path, PurePosixPath


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
    if (must_exist or path.exists()) and not path.is_file():
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
