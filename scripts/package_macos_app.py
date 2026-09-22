#!/usr/bin/env python3
"""Package an Elisa executable and project resources as a macOS .app bundle.

The generated launcher changes into the bundle's Resources directory before
starting the native executable. Elisa projects can therefore keep their
project-relative asset paths while a user launches the app from Finder.
"""

from __future__ import annotations

import argparse
import json
import plistlib
import re
import shutil
import stat
from pathlib import Path


class PackageError(ValueError):
    """The project or release bundle is not packageable."""


def load_project_manifest(project: Path) -> dict[str, object]:
    manifest = project / "elisa.project.json"
    if not manifest.is_file():
        return {}
    try:
        value = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackageError(f"could not read {manifest}: {error}") from error
    if not isinstance(value, dict):
        raise PackageError(f"{manifest} must contain a JSON object")
    return value


def manifest_application(manifest: dict[str, object], project: Path) -> tuple[str, str, Path]:
    raw_name = manifest.get("name", project.name)
    application = manifest.get("application", {})
    if not isinstance(application, dict):
        raise PackageError("project application settings must be an object")
    title = application.get("title", raw_name)
    if not isinstance(title, str) or not title.strip():
        raise PackageError("project application title must be a non-empty string")
    name = safe_bundle_name(title)
    output_value = manifest.get("output", f"build/{name}")
    if not isinstance(output_value, str) or not output_value or "\0" in output_value:
        raise PackageError("project output must be a non-empty path")
    executable = Path(output_value).expanduser()
    if not executable.is_absolute():
        executable = project / executable
    bundle_id = application.get("bundle_id", "")
    if not isinstance(bundle_id, str):
        raise PackageError("application bundle_id must be a string when provided")
    if not bundle_id:
        slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") or "application"
        bundle_id = f"org.elisa.{slug}"
    return name, bundle_id, executable.resolve()


def safe_bundle_name(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9 ._-]+", "", value).strip(" .")
    if not cleaned:
        raise PackageError("application name must contain a letter or number")
    return cleaned


def copy_directory(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise PackageError(f"required project directory is missing: {source}")
    shutil.copytree(source, destination, symlinks=False)


def write_launcher(path: Path, binary_name: str) -> None:
    script = f"""#!/bin/sh
set -eu
resources=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")/../Resources\" && pwd)\"
cd \"$resources\"
exec \"$resources/{binary_name}\" \"$@\"
"""
    path.write_text(script, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def package_app(project: Path, executable: Path, output: Path, name: str,
    bundle_id: str, version: str, icon: Path | None = None) -> Path:
    project = project.expanduser().resolve()
    executable = executable.expanduser().resolve()
    output = output.expanduser().resolve()
    if not project.is_dir():
        raise PackageError(f"project directory does not exist: {project}")
    if not executable.is_file():
        raise PackageError(f"built executable does not exist: {executable}")
    if icon is not None:
        icon = icon.expanduser().resolve()
        if not icon.is_file() or icon.suffix.lower() != ".icns":
            raise PackageError("app icon must be an existing .icns file")
    if not bundle_id or any(character.isspace() for character in bundle_id):
        raise PackageError("bundle identifier must be a non-empty token")
    bundle_name = safe_bundle_name(name)
    app = output if output.suffix == ".app" else output.with_suffix(".app")
    if app == project or app in project.parents:
        raise PackageError("bundle output must not replace or contain the source project")
    if app.exists():
        shutil.rmtree(app)

    contents = app / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir()

    binary_name = f"{bundle_name}.bin"
    shutil.copy2(executable, resources / binary_name)
    write_launcher(macos / bundle_name, binary_name)

    # Runtime paths in the game are deliberately project-relative. Keep the
    # source art and cooked packages together so the same bundle works without
    # the checkout, while source authoring files remain outside the executable.
    copy_directory(project / "assets", resources / "assets")
    copy_directory(project / "build" / "cooked", resources / "build" / "cooked")
    shaders = project / "shaders"
    if shaders.is_dir():
        copy_directory(shaders, resources / "shaders")

    info = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": bundle_name,
        "CFBundleExecutable": bundle_name,
        "CFBundleIdentifier": bundle_id,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": bundle_name,
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleVersion": version,
        "LSMinimumSystemVersion": "13.0",
        "NSHighResolutionCapable": True,
    }
    with (contents / "Info.plist").open("wb") as stream:
        if icon is not None:
            shutil.copy2(icon, resources / "AppIcon.icns")
            info["CFBundleIconFile"] = "AppIcon.icns"
        plistlib.dump(info, stream, sort_keys=False)
    return app


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True,
        help="Elisa project directory")
    parser.add_argument("--executable", type=Path,
        help="built executable (defaults to the manifest's output)")
    parser.add_argument("--output", type=Path,
        help=".app destination (defaults to project/build/<name>.app)")
    parser.add_argument("--name", help="display and launcher name (defaults to the manifest title)")
    parser.add_argument("--bundle-id", help="CFBundleIdentifier (defaults to org.elisa.<name>)")
    parser.add_argument("--version", default="0.1.0",
        help="CFBundleShortVersionString and CFBundleVersion")
    parser.add_argument("--icon", type=Path,
        help="optional .icns file copied into the bundle")
    return parser.parse_args()


def main() -> int:
    options = parse_arguments()
    try:
        project = options.project.expanduser().resolve()
        manifest = load_project_manifest(project)
        manifest_name, manifest_bundle_id, manifest_executable = manifest_application(
            manifest, project)
        name = safe_bundle_name(options.name) if options.name else manifest_name
        bundle_id = options.bundle_id or manifest_bundle_id
        executable = options.executable or manifest_executable
        output = options.output or project / "build" / f"{name}.app"
        icon = options.icon
        if icon is None:
            candidate = project / "resources" / "AppIcon.icns"
            icon = candidate if candidate.is_file() else None
        app = package_app(project, executable, output, name, bundle_id,
            options.version, icon)
    except (OSError, PackageError, ValueError) as error:
        print(f"macOS app packaging failed: {error}")
        return 1
    print(f"Packaged Elisa app: {app}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
