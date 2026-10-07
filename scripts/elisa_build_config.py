"""Argument, project-config and symbol-table helpers for elisa_build_run (split for the 600-line limit)."""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

from asset_cooks import BuildConfigurationError

PROJECT_MANIFEST = "elisa.project.json"


def configured_path(argument: str | None, *environment_names: str) -> Path | None:
    value = argument
    if value is None:
        value = next((os.environ[name] for name in environment_names if os.environ.get(name)), None)
    if value is None:
        return None
    return Path(value).expanduser().resolve()


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build or run an Elisa main() using the engine-owned Application host."
    )
    subparsers = parser.add_subparsers(dest="action", required=True)
    for action in ("build", "run"):
        command = subparsers.add_parser(action, help=f"{action} the Elisa project")
        command.add_argument("--project", required=True, help="project directory")
        command.add_argument("--main", help=f"Elisa main source; defaults to {PROJECT_MANIFEST}")
        command.add_argument("--output", help=f"executable path; defaults to {PROJECT_MANIFEST}")
        command.add_argument("--wicked-root", help="WickedEngine checkout (or WICKED_ROOT)")
        command.add_argument("--wicked-build", help="WickedEngine build directory (or WICKED_BUILD)")
        command.add_argument("--sdl3-root", help="SDL3 prefix with include/ and lib/ (or WICKED_SDL3_ROOT/SDL3_ROOT)")
        command.add_argument("--sdl3-include-dir", help="override SDL3 include directory")
        command.add_argument("--sdl3-lib-dir", help="override SDL3 library directory")
        command.add_argument("--brew-prefix", help="Homebrew prefix for freetype, harfbuzz, and zstd")
        command.add_argument("--brew-include-dir", help="override dependency include directory")
        command.add_argument("--brew-lib-dir", help="override dependency library directory")
        command.add_argument("--compiler", help="Elisa compiler (or ELISA_COMPILER_BIN)")
        command.add_argument("--runtime-object", help="Elisa runtime object (or ELISA_RUNTIME_OBJ)")
        command.add_argument("--cxx", help="native C++ compiler (or CXX)")
        command.add_argument("--no-public-runtime", action="store_true",
            help="compile only modules included by the entry source")
        command.add_argument("--native-test-probes", action="store_true",
            help="compile test-only native adapter fault-injection probes")
        command.add_argument("--force-cook-assets", action="store_true",
            help="ignore the asset cook cache and regenerate every declared asset")
        command.add_argument("--optimize", action="store_true",
            help="compile Elisa and the native runtime with -O2 (or set ELISA_NATIVE_OPTIMIZE=1)")
        command.add_argument("--console", action="store_true",
            help="build a headless console executable without the Application host "
                 "(or set \"host\": \"console\" in the manifest)")
        if action == "run":
            command.add_argument("program_args", nargs=argparse.REMAINDER,
                help="arguments after -- are passed to the program")
    return parser.parse_args(argv)


def load_project_config(project: Path) -> dict[str, object]:
    manifest = project / PROJECT_MANIFEST
    if not manifest.exists():
        return {}
    try:
        value = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise BuildConfigurationError(f"Could not read {manifest}: {error}") from error
    if not isinstance(value, dict):
        raise BuildConfigurationError(f"{manifest} must contain a JSON object")
    return value


def application_settings(config: dict[str, object]) -> dict[str, object]:
    application = config.get("application", {})
    if not isinstance(application, dict):
        raise BuildConfigurationError("project 'application' settings must be an object")
    title = application.get("title", "Elisa Engine")
    width = application.get("width", 1280)
    height = application.get("height", 720)
    hidden = application.get("hidden", False)
    try:
        title_size = len(title.encode("utf-8")) if isinstance(title, str) else 0
    except UnicodeEncodeError as error:
        raise BuildConfigurationError("application title must be valid UTF-8") from error
    if not isinstance(title, str) or "\0" in title or title_size < 1 or title_size >= 256:
        raise BuildConfigurationError("application title must contain 1 to 255 UTF-8 bytes")
    if isinstance(width, bool) or not isinstance(width, int) or width <= 0 or width > 16384:
        raise BuildConfigurationError("application width must be an integer from 1 to 16384")
    if isinstance(height, bool) or not isinstance(height, int) or height <= 0 or height > 16384:
        raise BuildConfigurationError("application height must be an integer from 1 to 16384")
    if not isinstance(hidden, bool):
        raise BuildConfigurationError("application hidden setting must be a boolean")
    return {"title": title, "width": width, "height": height, "hidden": hidden}


def parse_defined_global_symbols(output: str) -> set[str]:
    symbols: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 3:
            continue
        kind, name = fields[-2], fields[-1]
        if len(kind) == 1 and kind.upper() != "U":
            symbols.add(name)
    return symbols


def defined_global_symbols(path: Path) -> set[str]:
    nm = shutil.which("nm") or "/usr/bin/nm"
    result = subprocess.run([nm, "-g", str(path)], text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise BuildConfigurationError(f"Could not inspect Elisa object symbols in {path}: {result.stderr.strip()}")
    return parse_defined_global_symbols(result.stdout)


def project_host(config: dict[str, object], console_flag: bool) -> str:
    host = config.get("host", "application")
    if host not in ("application", "console"):
        raise BuildConfigurationError("project 'host' must be \"application\" or \"console\"")
    return "console" if console_flag else str(host)

