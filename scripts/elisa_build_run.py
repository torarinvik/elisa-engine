#!/usr/bin/env python3
"""Build or run an Elisa Application main against the engine-owned native host."""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from pathlib import PurePosixPath


ENGINE_ROOT = Path(__file__).resolve().parents[1]
PROJECT_MANIFEST = "elisa.project.json"
FRAMEWORKS = [
    "Foundation", "CoreFoundation", "CoreGraphics", "CoreText", "ImageIO",
    "Metal", "QuartzCore", "AppKit", "IOKit", "GameController", "AudioToolbox",
    "CoreAudio", "AVFoundation", "VideoToolbox", "Cocoa",
]


class BuildConfigurationError(Exception):
    pass


def configured_path(argument: str | None, *environment_names: str) -> Path | None:
    value = argument
    if value is None:
        value = next((os.environ[name] for name in environment_names if os.environ.get(name)), None)
    if value is None:
        return None
    return Path(value).expanduser().resolve()


def brew_prefix(formula: str | None = None) -> Path | None:
    brew = shutil.which("brew")
    if brew is None:
        return None
    command = [brew, "--prefix"]
    if formula:
        command.append(formula)
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        return None
    value = result.stdout.strip()
    return Path(value).expanduser().resolve() if value else None


def resolve_native_paths(args: argparse.Namespace) -> dict[str, Path]:
    wicked_root = configured_path(args.wicked_root, "WICKED_ROOT")
    if wicked_root is None:
        wicked_root = (ENGINE_ROOT.parent / "WickedEngine").resolve()
    wicked_build = configured_path(args.wicked_build, "WICKED_BUILD") or wicked_root / "build-elisa-sdl3"

    brew_root = configured_path(args.brew_prefix, "WICKED_BREW_PREFIX", "HOMEBREW_PREFIX")
    if brew_root is None:
        brew_root = brew_prefix()
    brew_include = configured_path(args.brew_include_dir, "WICKED_BREW_INCLUDE_DIR")
    brew_library = configured_path(args.brew_lib_dir, "WICKED_BREW_LIB_DIR")
    if brew_root is None and (brew_include is None or brew_library is None):
        raise BuildConfigurationError(
            "Homebrew dependencies are not configured; set --brew-prefix or WICKED_BREW_PREFIX, "
            "or set both WICKED_BREW_INCLUDE_DIR and WICKED_BREW_LIB_DIR"
        )
    if brew_root is not None:
        brew_include = brew_include or brew_root / "include"
        brew_library = brew_library or brew_root / "lib"

    sdl_root = configured_path(args.sdl3_root, "WICKED_SDL3_ROOT", "SDL3_ROOT")
    sdl_include = configured_path(args.sdl3_include_dir, "WICKED_SDL3_INCLUDE_DIR")
    sdl_library = configured_path(args.sdl3_lib_dir, "WICKED_SDL3_LIB_DIR")
    if sdl_root is None and (sdl_include is None or sdl_library is None):
        sdl_root = brew_prefix("sdl3")
    if sdl_root is None and (sdl_include is None or sdl_library is None):
        raise BuildConfigurationError(
            "SDL3 is not configured; set --sdl3-root or WICKED_SDL3_ROOT, "
            "or set both WICKED_SDL3_INCLUDE_DIR and WICKED_SDL3_LIB_DIR"
        )
    if sdl_root is not None:
        sdl_include = sdl_include or sdl_root / "include"
        sdl_library = sdl_library or sdl_root / "lib"

    assert brew_include is not None and brew_library is not None
    assert sdl_include is not None and sdl_library is not None
    return {
        "wicked_root": wicked_root,
        "wicked_build": wicked_build,
        "wicked_source": wicked_root / "WickedEngine",
        "libraries": wicked_build / "WickedEngine",
        "miniaudio_include": ENGINE_ROOT / "dependencies/miniaudio",
        "sdl_include": sdl_include,
        "sdl_library": sdl_library,
        "brew_include": brew_include,
        "brew_library": brew_library,
    }


def required_native_files(paths: dict[str, Path]) -> list[Path]:
    utility = paths["libraries"] / "Utility"
    return [
        paths["wicked_source"] / "wiApplication.h",
        paths["wicked_source"] / "wiAppleHelper.mm",
        paths["wicked_source"] / "wiInput_Apple.mm",
        paths["wicked_source"] / "shaders",
        paths["wicked_source"] / "libdxcompiler.dylib",
        paths["libraries"] / "libWickedEngine.a",
        paths["libraries"] / "libJolt.a",
        utility / "libUtility.a",
        utility / "FAudio/libFAudio.a",
        paths["libraries"] / "LUA/libLUA.a",
        paths["sdl_include"] / "SDL3/SDL.h",
        paths["sdl_library"] / "libSDL3.dylib",
        paths["brew_include"] / "freetype2/ft2build.h",
        paths["brew_include"] / "harfbuzz/hb.h",
        paths["miniaudio_include"] / "miniaudio.h",
    ] + [
        paths["brew_library"] / f"lib{name}.{suffix}"
        for name in ("freetype", "harfbuzz", "zstd")
        for suffix in ("dylib", "a")
        if not any((paths["brew_library"] / f"lib{name}.{candidate}").exists()
            for candidate in ("dylib", "a"))
    ]


def validate_native_files(paths: dict[str, Path]) -> None:
    missing = [path for path in required_native_files(paths) if not path.exists()]
    if missing:
        details = "\n  ".join(str(path) for path in missing)
        raise BuildConfigurationError("Missing native build dependencies:\n  " + details)


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
        command.add_argument("--cxx", help="native C++ compiler (or CXX)")
    return parser.parse_args(argv)


def print_command(command: list[str]) -> None:
    print("+", shlex.join(command), flush=True)


def run_command(command: list[str], *, cwd: Path | None = None,
    env: dict[str, str] | None = None) -> int:
    print_command(command)
    try:
        return subprocess.run(command, cwd=cwd, env=env, check=False).returncode
    except OSError as error:
        print(f"Could not start {command[0]!r}: {error}", file=sys.stderr)
        return 127


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


def cook_declared_assets(project: Path, config: dict[str, object]) -> int:
    declarations = config.get("asset_cooks", [])
    if not isinstance(declarations, list) or len(declarations) > 64:
        raise BuildConfigurationError("project 'asset_cooks' must be an array of at most 64 entries")
    cooker = ENGINE_ROOT / "scripts/cook_fbx_asset.py"
    for index, declaration in enumerate(declarations):
        if not isinstance(declaration, dict):
            raise BuildConfigurationError(f"asset_cooks[{index}] must be an object")
        source = declared_project_path(project, declaration.get("source"), f"asset_cooks[{index}].source", must_exist=True)
        output = declared_project_path(project, declaration.get("output"), f"asset_cooks[{index}].output", must_exist=False)
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
        print(f"Cooking project asset: {source.relative_to(project)} -> {output.relative_to(project)}", flush=True)
        command = [sys.executable, str(cooker), str(source), "--asset-path", asset_path,
            "--output", str(output)]
        if max_triangles is not None:
            command.extend(["--max-triangles", str(max_triangles)])
        status = run_command(command, cwd=project)
        if status != 0:
            return status
    return 0


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


def resolve_project_paths(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    project = Path(args.project).expanduser().resolve()
    if not project.is_dir():
        raise BuildConfigurationError(f"Project directory does not exist: {project}")
    config = load_project_config(project)
    application_settings(config)
    main_value = args.main or config.get("main")
    if not isinstance(main_value, str) or not main_value:
        raise BuildConfigurationError(f"provide --main or set 'main' in {PROJECT_MANIFEST}")
    if chr(0) in main_value:
        raise BuildConfigurationError("Elisa main source path must not contain a NUL character")
    main_source = Path(main_value).expanduser()
    if not main_source.is_absolute():
        main_source = project / main_source
    main_source = main_source.resolve()
    if not main_source.is_file() or main_source.suffix != ".elisa":
        raise BuildConfigurationError(f"Elisa main source does not exist: {main_source}")
    output_value = args.output or config.get("output")
    if output_value is None:
        name = config.get("name", project.name)
        if not isinstance(name, str) or not name:
            raise BuildConfigurationError("project 'name' must be a non-empty string")
        if chr(0) in name:
            raise BuildConfigurationError("project 'name' must not contain a NUL character")
        output_value = f"build/{name}"
    if not isinstance(output_value, str) or not output_value:
        raise BuildConfigurationError(f"provide --output or set 'output' in {PROJECT_MANIFEST}")
    if chr(0) in output_value:
        raise BuildConfigurationError("output path must not contain a NUL character")
    output = Path(output_value).expanduser()
    if not output.is_absolute():
        output = project / output
    output = output.resolve()
    if output.exists() and output.is_dir():
        raise BuildConfigurationError(f"Output path is a directory: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    return project, main_source, output


def write_entry_wrapper(destination: Path, main_source: Path) -> None:
    runtime_bundle = ENGINE_ROOT / "src/runtime/public.elisa"
    includes = (runtime_bundle, main_source)
    for path in includes:
        if any(character in str(path) for character in ('"', "\n", "\r")):
            raise BuildConfigurationError(f"Elisa include path contains an unsupported quote or newline: {path}")
    destination.write_text(
        f'include "{runtime_bundle}"\ninclude "{main_source}"\n',
        encoding="utf-8",
    )


def compile_archive(compiler: str, wrapper: Path, archive: Path) -> int:
    return run_command([compiler, "-emit", "c-archive", "-o", str(archive), str(wrapper)])


def audit_archive(archive: Path) -> None:
    manifest = archive.with_suffix(".elisa-abi.json")
    if not manifest.is_file():
        raise BuildConfigurationError("Elisa compiler did not emit the ABI audit manifest")
    try:
        abi = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BuildConfigurationError(f"Could not read Elisa ABI manifest: {error}") from error
    if any(abi.get(field) for field in ("exported_functions", "exported_globals", "exported_types")):
        raise BuildConfigurationError("Application projects must not define game-owned C exports")


def native_link_command(cxx: str, archive: Path, staged_output: Path,
    build_dir: Path, paths: dict[str, Path]) -> list[str]:
    wicked_source = paths["wicked_source"]
    libraries = paths["libraries"]
    utility = libraries / "Utility"
    utility_source = wicked_source / "Utility"
    sdl_include = paths["sdl_include"]
    sdl_library = paths["sdl_library"]
    brew_include = paths["brew_include"]
    brew_library = paths["brew_library"]
    command = [
        cxx, "-std=c++17", "-O0", "-include", "filesystem", "-DWI_UNORDERED_MAP_TYPE=2",
        "-DWICKED_CMAKE_BUILD", "-DSDL3=1", "-D__OBJC_BOOL_IS_BOOL=1",
        "-I", str(build_dir), "-I", str(ENGINE_ROOT / "native"), "-I", str(wicked_source),
        "-I", str(utility_source), "-I", str(utility_source / "metal"),
        "-I", str(utility_source / "DirectXMath"),
        "-I", str(sdl_include), "-I", str(sdl_include / "SDL3"),
        "-I", str(brew_include), "-I", str(brew_include / "freetype2"),
        "-I", str(brew_include / "harfbuzz"),
        "-I", str(paths["miniaudio_include"]),
        str(ENGINE_ROOT / "native/application_abi.cpp"),
        str(ENGINE_ROOT / "native/render_scene_abi.cpp"),
        str(ENGINE_ROOT / "native/elisa_native_fallbacks.cpp"),
        str(ENGINE_ROOT / "native/audio_service_abi.cpp"),
        str(ENGINE_ROOT / "native/miniaudio_implementation.cpp"),
        str(wicked_source / "wiAppleHelper.mm"), str(wicked_source / "wiInput_Apple.mm"),
        str(archive), str(libraries / "libWickedEngine.a"), str(libraries / "libJolt.a"),
        str(utility / "libUtility.a"), str(utility / "FAudio/libFAudio.a"),
        str(libraries / "LUA/libLUA.a"),
        "-L", str(sdl_library), "-lSDL3", "-L", str(brew_library),
        "-lfreetype", "-lharfbuzz", "-lzstd", "-Wl,-rpath,@executable_path",
        "-Wl,-rpath," + str(wicked_source),
    ]
    for framework in FRAMEWORKS:
        command.extend(["-framework", framework])
    command.extend(["-o", str(staged_output)])
    return command


def build_project(args: argparse.Namespace) -> tuple[int, Path | None, Path | None]:
    if sys.platform != "darwin":
        print("The SDL3/Metal Application host currently supports macOS only.", file=sys.stderr)
        return 2, None, None
    project, main_source, output = resolve_project_paths(args)
    config = load_project_config(project)
    paths = resolve_native_paths(args)
    validate_native_files(paths)
    status = cook_declared_assets(project, config)
    if status != 0:
        return status, None, None
    compiler = args.compiler or os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    cxx = args.cxx or os.environ.get("CXX", "clang++")

    with tempfile.TemporaryDirectory(prefix="Elisa application build ", dir=output.parent) as temporary_directory:
        build_dir = Path(temporary_directory)
        wrapper = build_dir / "application_entry.elisa"
        archive = build_dir / "application_entry.a"
        staged_output = build_dir / "application"
        write_entry_wrapper(wrapper, main_source)
        status = compile_archive(compiler, wrapper, archive)
        if status != 0:
            return status, None, None
        audit_archive(archive)
        command = native_link_command(cxx, archive, staged_output, build_dir, paths)
        status = run_command(command, cwd=project)
        if status != 0:
            return status, None, None
        if not staged_output.is_file():
            print("Native linker succeeded without creating the output executable.", file=sys.stderr)
            return 1, None, None
        output.parent.mkdir(parents=True, exist_ok=True)
        os.replace(staged_output, output)
    return 0, output, paths["wicked_source"] / "shaders"


def main(argv: list[str] | None = None) -> int:
    try:
        args = parse_arguments(argv)
        status, output, shader_path = build_project(args)
        if status != 0 or output is None:
            return status
        if args.action == "build":
            print(f"Built Elisa application: {output}")
            return 0
        runtime_env = dict(os.environ)
        runtime_env["ELISA_ENGINE_SHADER_PATH"] = str(shader_path)
        project = Path(args.project).expanduser().resolve()
        app = application_settings(load_project_config(project))
        runtime_env["ELISA_PROJECT_TITLE"] = str(app["title"])
        runtime_env["ELISA_PROJECT_WIDTH"] = str(app["width"])
        runtime_env["ELISA_PROJECT_HEIGHT"] = str(app["height"])
        runtime_env["ELISA_PROJECT_HIDDEN"] = "1" if app["hidden"] else "0"
        runtime_env["ELISA_PROJECT_ROOT"] = str(project)
        return run_command([str(output)], cwd=project, env=runtime_env)
    except BuildConfigurationError as error:
        print(f"elisa-build-run: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
