#!/usr/bin/env python3
"""Package an Elisa executable and project resources as a macOS .app bundle.

The generated launcher changes into the bundle's Resources directory before
starting the native executable. Elisa projects can therefore keep their
project-relative asset paths while a user launches the app from Finder.

The manifest's optional ``package.resources`` list names the project-relative
files and directories the built game reads at runtime. When it is present only
those paths (none, for an empty list), the cooked packages and the shader
directory are staged, so source art, authoring files and the assets
repository's own history stay out of the bundle. Without the list the whole ``assets`` directory is staged, minus version
control and editor litter.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import plistlib
import re
import shlex
import shutil
import stat
import subprocess
import tempfile
import time
from pathlib import Path

from macos_deployment_target import METAL_MINIMUM_MACOS, deployment_targets, format_version
from macos_bundle_support import (  # noqa: F401 - re-exported for callers and tests
    IGNORED_NAMES, MACH_O_MAGICS, MAX_BUNDLED_LIBRARIES, MAX_RESOURCE_ENTRIES,
    SHADER_BACKENDS, SHADER_BINARY_SUFFIXES, SHADER_GENERATED_INVENTORY_NAME,
    SHADER_MANIFEST_NAME, SHADER_MANIFEST_SCHEMA, SHADER_METADATA_SUFFIX,
    SYSTEM_LIBRARY_PREFIXES, PackageError, bundle_dynamic_libraries, existing_rpaths,
    executable_provenance, ignore_litter, ignore_shader_metadata, is_mach_o,
    linked_libraries, run_tool, shader_manifest,
)


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


def manifest_window(manifest: dict[str, object], project: Path) -> tuple[str, int, int]:
    """Return the project window title and size the launcher must pass on."""
    application = manifest.get("application", {})
    if not isinstance(application, dict):
        raise PackageError("project application settings must be an object")
    title = application.get("title", manifest.get("name", project.name))
    if not isinstance(title, str) or not title.strip():
        raise PackageError("project application title must be a non-empty string")
    size = []
    for key, default in (("width", 1280), ("height", 720)):
        value = application.get(key, default)
        if isinstance(value, bool) or not isinstance(value, int) or not 0 < value <= 16384:
            raise PackageError(f"project application {key} must be an integer from 1 to 16384")
        size.append(value)
    return title, size[0], size[1]


def safe_bundle_name(value: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9 ._-]+", "", value).strip(" .")
    if not cleaned:
        raise PackageError("application name must contain a letter or number")
    return cleaned


def copy_directory(source: Path, destination: Path, ignore=ignore_litter,
    *, merge: bool = False) -> None:
    if not source.is_dir():
        raise PackageError(f"required project directory is missing: {source}")
    if source.is_symlink():
        raise PackageError(f"package directory must not be a symbolic link: {source}")
    def checked_ignore(directory: str, names: list[str]) -> set[str]:
        ignored = set(ignore(directory, names))
        for name in names:
            entry = Path(directory) / name
            if name not in ignored and entry.is_symlink():
                raise PackageError(f"package directory contains a symbolic link: {entry}")
        return ignored
    shutil.copytree(source, destination, symlinks=False, ignore=checked_ignore, dirs_exist_ok=merge)


def copy_compiled_shaders(source: Path, destination: Path) -> None:
    """Stage compiled backend binaries without source or build metadata."""
    if not source.is_dir():
        raise PackageError(f"required project directory is missing: {source}")
    destination.mkdir(parents=True)
    copied = False
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            raise PackageError(f"shader tree contains a symbolic link: {path}")
        if not path.is_file() or path.suffix.lower() not in SHADER_BINARY_SUFFIXES:
            continue
        relative = path.relative_to(source)
        if len(relative.parts) < 2 or relative.parts[0] not in SHADER_BACKENDS:
            raise PackageError(f"compiled shader is outside a known backend directory: {relative}")
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        copied = True
    if not copied:
        raise PackageError("compiled-shaders-only staging found no compiled shader binaries under "
            f"{source}")


def manifest_resources(manifest: dict[str, object], project: Path) -> list[Path] | None:
    """Return the manifest's runtime resource paths, or None to stage all assets."""
    package = manifest.get("package")
    if package is None:
        return None
    if not isinstance(package, dict):
        raise PackageError("manifest 'package' must be a JSON object")
    raw = package.get("resources")
    if raw is None:
        return None
    # An empty list declares a game with no runtime resource files (for
    # example procedural content); only the executable, shaders and notices
    # are staged.
    if not isinstance(raw, list):
        raise PackageError("manifest 'package.resources' must be a list")
    if len(raw) > MAX_RESOURCE_ENTRIES:
        raise PackageError(f"manifest 'package.resources' lists more than {MAX_RESOURCE_ENTRIES} entries")
    resources: list[Path] = []
    for entry in raw:
        if not isinstance(entry, str) or not entry.strip():
            raise PackageError("manifest 'package.resources' entries must be non-empty strings")
        relative = Path(entry)
        if relative.is_absolute() or ".." in relative.parts:
            raise PackageError(f"manifest resource must stay inside the project: {entry}")
        if any(part in IGNORED_NAMES for part in relative.parts):
            raise PackageError(f"manifest resource names version-control or editor litter: {entry}")
        if relative not in resources:
            resources.append(relative)
    return resources


def manifest_notices(manifest: dict[str, object], project: Path) -> list[Path]:
    package = manifest.get("package", {})
    if not isinstance(package, dict):
        raise PackageError("manifest 'package' must be an object")
    entries = package.get("notices", [])
    if not isinstance(entries, list) or len(entries) > MAX_RESOURCE_ENTRIES:
        raise PackageError("package.notices must be a bounded list of project-relative files")
    paths = []
    for entry in entries:
        if not isinstance(entry, str) or not entry or "\0" in entry:
            raise PackageError("package.notices entries must be non-empty file paths")
        relative = Path(entry)
        if relative.is_absolute() or ".." in relative.parts or relative in paths:
            raise PackageError(f"invalid or duplicate notice path: {entry}")
        source = project / relative
        resolved = source.resolve()
        if (not resolved.is_relative_to(project.resolve()) or source.is_symlink()
                or not source.is_file() or source.stat().st_size == 0):
            raise PackageError(f"notice must be a nonempty file inside the project: {entry}")
        paths.append(relative)
    return paths


def stage_resource(project: Path, resources: Path, relative: Path) -> None:
    source = project / relative
    destination = resources / relative
    # Checking only the leaf misses a symlink in a parent directory and can
    # silently copy files from outside the project into a supposedly closed bundle.
    component = project
    for part in relative.parts:
        component = component / part
        if component.is_symlink():
            raise PackageError(f"manifest resource must not be a symbolic link: {component.relative_to(project)} (resource {relative})")
    if source.is_dir():
        for child in source.rglob("*"):
            if child.is_symlink():
                child_relative = child.relative_to(project)
                raise PackageError(
                    f"manifest resource contains a symbolic link: {child_relative}")
        copy_directory(source, destination)
    elif source.is_file():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    else:
        raise PackageError(f"manifest resource is missing from the project: {relative}")


def source_revision(project: Path) -> str:
    """Return the packaged project's git revision, marking uncommitted changes."""
    try:
        revision = subprocess.run(["git", "-C", str(project), "rev-parse", "HEAD"],
            capture_output=True, text=True, check=True, timeout=10).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(project), "status", "--porcelain", "--", "."],
            capture_output=True, text=True, check=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    return f"{revision}-dirty" if dirty else revision


def executable_build_identity(executable: Path) -> str:
    sidecar = executable.with_name(executable.name + ".provenance.json")
    try:
        record = json.loads(sidecar.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return "unavailable"
    identity = record.get("build_identity") if isinstance(record, dict) else None
    if not isinstance(identity, str) or re.fullmatch(r"[0-9a-fA-F]{16}", identity) is None:
        return "unavailable"
    return identity.lower()


def write_launcher(path: Path, binary_name: str,
    window: tuple[str, int, int] = ("Elisa Engine", 1280, 720), identity: str = "",
    bundle_id: str = "org.elisa.application", build_identity: str = "unavailable") -> None:
    # The runtime reads its window settings from the environment, which the
    # build runner sets from elisa.project.json. A double-clicked bundle has
    # no runner, so the launcher supplies the same defaults while still
    # letting an explicitly exported value win. The identity line goes to
    # stderr first so every log names the build that produced it.
    title, width, height = window
    announce = f"printf '%s\\n' {shlex.quote(identity)} >&2 || :\n" if identity else ""
    crash_ips_pattern = shlex.quote(f"{path.name}*.ips")
    crash_legacy_pattern = shlex.quote(f"{path.name}*.crash")
    crash_identity = (f"if [ -z \"${{ELISA_BUILD_IDENTITY:-}}\" ]; then ELISA_BUILD_IDENTITY={shlex.quote(build_identity)}; fi\n"
        "export ELISA_BUILD_IDENTITY\n") if identity else ""
    script = f"""#!/bin/sh
set -eu
resources=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")/../Resources\" && pwd)\"
cd \"$resources\"
: \"${{ELISA_PROJECT_TITLE:={shlex.quote(title)}}}\"
: \"${{ELISA_PROJECT_WIDTH:={width}}}\"
: \"${{ELISA_PROJECT_HEIGHT:={height}}}\"
export ELISA_PROJECT_TITLE ELISA_PROJECT_WIDTH ELISA_PROJECT_HEIGHT
# Local crash reports only: the runtime writes crash-PID.txt here on a fatal
# signal. An exported ELISA_CRASH_DIR wins; an uncreatable one turns them off.
if [ -z \"${{ELISA_CRASH_DIR:-}}\" ] && [ -n \"${{HOME:-}}\" ]; then
    ELISA_CRASH_DIR=\"$HOME/Library/Logs/{Path(binary_name).stem}\"
fi
if [ -n \"${{ELISA_CRASH_DIR:-}}\" ] && mkdir -p \"$ELISA_CRASH_DIR\" 2>/dev/null; then
    export ELISA_CRASH_DIR
else
    unset ELISA_CRASH_DIR
fi
{crash_identity}if [ -d \"$resources/shaders\" ]; then
    ELISA_ENGINE_SHADER_PATH=\"$resources/shaders\"
    export ELISA_ENGINE_SHADER_PATH
    if [ -f \"$resources/shaders/{SHADER_MANIFEST_NAME}\" ]; then
        ELISA_ENGINE_SHADER_MANIFEST=\"$resources/shaders/{SHADER_MANIFEST_NAME}\"
        export ELISA_ENGINE_SHADER_MANIFEST
    fi
fi
{announce}log_directory=\"${{HOME:-}}/Library/Logs/Elisa\"
log_directory=\"$log_directory\"/{shlex.quote(bundle_id)}
log_file=\"\"
if [ -n \"${{HOME:-}}\" ] && mkdir -p \"$log_directory\" 2>/dev/null; then
    log_file=\"$log_directory/latest.log\"
    if [ -f \"$log_file\" ]; then
        cp \"$log_file\" \"$log_directory/previous.log\" 2>/dev/null || :
    fi
    if ! {{
        printf '%s\\n' {shlex.quote(identity or 'Elisa application')}
        printf 'build_identity=%s\\n' {shlex.quote(build_identity)}
        printf 'started_utc=%s\\n' \"$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf unknown)\"
        printf '%s\\n' '--- process output ---'
    }} > \"$log_file\" 2>/dev/null; then
        log_file=\"\"
    fi
fi
if [ -n \"$log_file\" ]; then
    printf 'launcher_log=%s\\n' \"$log_file\" >&2 || :
    if \"$resources/{binary_name}\" \"$@\" >> \"$log_file\" 2>&1; then
        status=0
    else
        status=$?
    fi
    printf '\\nprocess_exit_status=%s\\n' \"$status\" >> \"$log_file\" 2>/dev/null || :
else
    if \"$resources/{binary_name}\" \"$@\"; then
        status=0
    else
        status=$?
    fi
fi
if [ \"$status\" -ne 0 ]; then
    home_directory=\"$(/usr/bin/printenv HOME 2>/dev/null || :)\"
    diagnostic_reports=\"$home_directory/Library/Logs/DiagnosticReports\"
    if [ -n \"$home_directory\" ] && [ -d \"$diagnostic_reports\" ]; then
        if [ -n \"$log_file\" ]; then
            {{
                printf '%s\\n' 'recent macOS crash report candidates (match timestamps to started_utc):'
                find \"$diagnostic_reports\" -type f \\
                    \\( -name {crash_ips_pattern} -o -name {crash_legacy_pattern} \\) \\
                    -mtime -1 -print
            }} >> \"$log_file\" 2>/dev/null || :
        else
            printf '%s\\n' 'recent macOS crash report candidates (match timestamps to this run):' >&2 || :
            find \"$diagnostic_reports\" -type f \\
                \\( -name {crash_ips_pattern} -o -name {crash_legacy_pattern} \\) \\
                -mtime -1 -print >&2 2>/dev/null || :
        fi
    fi
fi
printf 'Elisa process exit status: %s\\n' \"$status\" >&2 || :
exit \"$status\"
"""
    path.write_text(script, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def package_app(project: Path, executable: Path, output: Path, name: str,
    bundle_id: str, version: str, icon: Path | None = None,
    resource_paths: list[Path] | None = None,
    window: tuple[str, int, int] | None = None,
    shader_root: Path | None = None,
    notice_paths: list[Path] | None = None,
    compiled_shaders_only: bool = False) -> Path:
    project = project.expanduser().resolve()
    output = output.expanduser().resolve()
    app = output if output.suffix == ".app" else output.with_suffix(".app")
    if app == project or app in project.parents:
        raise PackageError("bundle output must not replace or contain the source project")
    inputs = [executable, shader_root or project / "shaders", project / "build/cooked"]
    inputs.extend(project / path for path in (resource_paths if resource_paths is not None else [Path("assets")]))
    inputs.extend(project / path for path in notice_paths or [])
    if icon is not None:
        inputs.append(icon)
    for source in inputs:
        source = source.expanduser().resolve()
        if (app == source or app in source.parents or
                source in app.parents):
            raise PackageError(f"bundle output overlaps a required packaging input: {source}")
    if app.exists() and not app.is_dir():
        raise PackageError(f"bundle destination is not a directory: {app}")
    app.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".elisa-app-stage-", dir=app.parent) as folder:
        staged = _assemble_app(project, executable, Path(folder) / app.name, name,
            bundle_id, version, icon, resource_paths, window, shader_root, notice_paths,
            compiled_shaders_only)
        backup = Path(tempfile.mkdtemp(prefix=".elisa-app-backup-", dir=app.parent))
        backup.rmdir()
        previous = app.exists()
        if previous:
            os.replace(app, backup)
        try:
            os.replace(staged, app)
        except OSError:
            if previous:
                try:
                    os.replace(backup, app)
                except OSError as error:
                    raise PackageError(f"app publication and rollback failed; previous bundle retained at {backup}") from error
            raise
        if previous:
            # Publication has committed. Cleanup cannot report a failed build
            # after replacing the previous bundle; leftover backups are recoverable.
            shutil.rmtree(backup, ignore_errors=True)
    return app


def _assemble_app(project: Path, executable: Path, output: Path, name: str,
    bundle_id: str, version: str, icon: Path | None = None,
    resource_paths: list[Path] | None = None,
    window: tuple[str, int, int] | None = None,
    shader_root: Path | None = None,
    notice_paths: list[Path] | None = None,
    compiled_shaders_only: bool = False) -> Path:
    project = project.expanduser().resolve()
    executable = executable.expanduser().resolve()
    output = output.expanduser().resolve()
    if not project.is_dir():
        raise PackageError(f"project directory does not exist: {project}")
    notices = manifest_notices({"package": {"notices": [str(path) for path in notice_paths or []]}}, project)
    if not executable.is_file():
        raise PackageError(f"built executable does not exist: {executable}")
    provenance = executable_provenance(executable)
    if shader_root is not None:
        shader_root = shader_root.expanduser().resolve()
        if not shader_root.is_dir():
            raise PackageError(f"prepared shader directory does not exist: {shader_root}")
    if icon is not None:
        icon = icon.expanduser().resolve()
        if not icon.is_file() or icon.suffix.lower() != ".icns":
            raise PackageError("app icon must be an existing .icns file")
    if not bundle_id or re.fullmatch(r"[A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+)*", bundle_id) is None:
        raise PackageError("bundle identifier must be dot-separated alphanumeric, underscore or hyphen tokens")
    bundle_name = safe_bundle_name(name)
    app = output if output.suffix == ".app" else output.with_suffix(".app")
    if app == project or app in project.parents:
        raise PackageError("bundle output must not replace or contain the source project")
    for source in (executable, shader_root, icon, *(project / relative for relative in notices)):
        if source is not None and (source == app or app in source.parents):
            raise PackageError(f"bundle output contains a required packaging input: {source}")
    if app.exists():
        shutil.rmtree(app)

    contents = app / "Contents"
    macos = contents / "MacOS"
    resources = contents / "Resources"
    macos.mkdir(parents=True)
    resources.mkdir()

    binary_name = f"{bundle_name}.bin"
    for relative in resource_paths or []:
        # Resource paths are copied after the executable. Refuse both an exact
        # overwrite and a path that would treat the executable as a directory.
        if relative.parts and relative.parts[0].casefold() == binary_name.casefold():
            raise PackageError(f"manifest resource conflicts with packaged executable: {relative}")
    # Hash the built executable before load-command edits and re-signing so
    # the identity matches the build runner's output.
    build_identity = executable_build_identity(executable)
    identity = (f"Elisa package: {bundle_name} {version} ({bundle_id}) "
        f"executable-sha256={hashlib.sha256(executable.read_bytes()).hexdigest()} "
        f"build-identity={build_identity} source={source_revision(project)}")
    shutil.copy2(executable, resources / binary_name)
    if is_mach_o(resources / binary_name):
        bundle_dynamic_libraries(resources / binary_name, contents / "Frameworks")
    write_launcher(macos / bundle_name, binary_name, window or (name, 1280, 720),
        identity, bundle_id, build_identity)

    # Runtime paths in the game are deliberately project-relative. Stage the
    # declared runtime resources (or, without a declaration, the whole assets
    # directory) next to the cooked packages so the bundle works without the
    # checkout, while authoring files and repository history stay outside it.
    if resource_paths is None:
        copy_directory(project / "assets", resources / "assets")
    else:
        for relative in resource_paths:
            stage_resource(project, resources, relative)
    cooked = project / "build" / "cooked"
    if cooked.exists():
        # Explicit resources may already stage this directory or individual cooked
        # files. Both copies originate from the same project-relative source.
        copy_directory(cooked, resources / "build" / "cooked", merge=True)
    shaders = shader_root if shader_root is not None else project / "shaders"
    if shaders.is_dir():
        if compiled_shaders_only:
            copy_compiled_shaders(shaders, resources / "shaders")
        else:
            copy_directory(shaders, resources / "shaders", ignore_shader_metadata)
        manifest = shader_manifest(resources / "shaders")
        (resources / "shaders" / SHADER_MANIFEST_NAME).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if provenance is not None:
        provenance_destination = resources / "build-provenance.json"
        if provenance_destination.exists():
            raise PackageError("a packaged resource conflicts with build-provenance.json")
        provenance["project_root"] = "<project>"
        try:
            provenance["main_source"] = Path(str(provenance.get("main_source", ""))).resolve().relative_to(project.resolve()).as_posix()
        except (OSError, ValueError):
            provenance["main_source"] = Path(str(provenance.get("main_source", ""))).name
        repositories = provenance.get("repositories")
        if isinstance(repositories, dict):
            for label, identity in repositories.items():
                if isinstance(identity, dict):
                    identity["root"] = f"<{label}>"
        tools = provenance.get("tools")
        if isinstance(tools, dict):
            for tool in tools.values():
                if isinstance(tool, dict):
                    for field in ("requested", "resolved"):
                        value = tool.get(field)
                        if isinstance(value, str):
                            tool[field] = Path(value).name
                    if isinstance(tool.get("path"), str):
                        tool["path"] = Path(str(tool["path"])).name
        native = provenance.get("native_link_artifacts")
        if isinstance(native, list):
            for item in native:
                if isinstance(item, dict) and isinstance(item.get("path"), str):
                    item["path"] = Path(str(item["path"])).name
        options = provenance.get("options")
        if isinstance(options, dict):
            for field in ("wicked_build", "compiler_request", "native_compiler_request"):
                if isinstance(options.get(field), str):
                    options[field] = Path(str(options[field])).name
        binary = provenance.get("binary")
        if isinstance(binary, dict):
            binary["path"] = "build executable"
        packaged_binary = resources / binary_name
        provenance["packaged_binary"] = {
            "path": binary_name,
            "sha256": hashlib.sha256(packaged_binary.read_bytes()).hexdigest(),
            "size_bytes": packaged_binary.stat().st_size,
        }
        provenance_destination.write_text(json.dumps(provenance, indent=2, sort_keys=True) + "\n",
            encoding="utf-8")

    for relative in notices:
        destination = resources / "Notices" / relative
        if destination.exists():
            raise PackageError(f"notice destination conflicts with a staged resource: {relative}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(project / relative, destination)

    minimum_os = METAL_MINIMUM_MACOS
    native_files = [resources / binary_name]
    frameworks = contents / "Frameworks"
    if frameworks.exists():
        native_files.extend(path for path in frameworks.rglob("*") if path.is_file())
    for native_file in native_files:
        if is_mach_o(native_file):
            try:
                minimum_os = max(minimum_os, *deployment_targets(run_tool("otool", "-l", str(native_file))))
            except ValueError as error:
                raise PackageError(f"cannot determine deployment target for {native_file.name}: {error}") from error

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
        "LSMinimumSystemVersion": format_version(minimum_os),
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
    parser.add_argument("--shader-root", type=Path,
        help="prepared shader library to stage (default: project/shaders)")
    parser.add_argument("--compiled-shaders-only", action="store_true",
        help="stage compiled backend binaries only; runtime source-compilation fallback is unavailable")
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
        stage_started = time.perf_counter()
        app = package_app(project, executable, output, name, bundle_id,
            options.version, icon, manifest_resources(manifest, project),
            manifest_window(manifest, project), options.shader_root,
            manifest_notices(manifest, project), options.compiled_shaders_only)
        print(f"App packaging: {time.perf_counter() - stage_started:.2f}s", flush=True)
    except (OSError, PackageError, ValueError) as error:
        print(f"macOS app packaging failed: {error}")
        return 1
    print(f"Packaged Elisa app: {app}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
