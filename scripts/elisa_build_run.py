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


ENGINE_ROOT = Path(__file__).resolve().parents[1]
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
        command.add_argument("--main", required=True, help="Elisa main source, relative to --project or absolute")
        command.add_argument("--output", required=True, help="executable path, relative to --project or absolute")
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


def resolve_project_paths(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    project = Path(args.project).expanduser().resolve()
    if not project.is_dir():
        raise BuildConfigurationError(f"Project directory does not exist: {project}")
    main_source = Path(args.main).expanduser()
    if not main_source.is_absolute():
        main_source = project / main_source
    main_source = main_source.resolve()
    if not main_source.is_file() or main_source.suffix != ".elisa":
        raise BuildConfigurationError(f"Elisa main source does not exist: {main_source}")
    output = Path(args.output).expanduser()
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
        str(ENGINE_ROOT / "native/application_abi.cpp"),
        str(ENGINE_ROOT / "native/render_scene_abi.cpp"),
        str(ENGINE_ROOT / "native/elisa_native_fallbacks.cpp"),
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
    paths = resolve_native_paths(args)
    validate_native_files(paths)
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
        return run_command([str(output)], cwd=Path(args.project).expanduser().resolve(), env=runtime_env)
    except BuildConfigurationError as error:
        print(f"elisa-build-run: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
