"""Native toolchain and library resolution for elisa_build_run.

Locates the compiler, Elisa runtime object, Homebrew prefixes and the Wicked,
Jolt and ozz native trees a project host links against.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

from asset_cooks import BuildConfigurationError


ENGINE_ROOT = Path(__file__).resolve().parents[1]


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
        manifest_path = ENGINE_ROOT / "native/dependency-manifest.json"
        try:
            dependencies = json.loads(manifest_path.read_text(encoding="utf-8"))
            wicked = next(library for library in dependencies["libraries"]
                          if library["name"] == "WickedEngine")
            wicked_root = (ENGINE_ROOT / wicked["path_default"]).resolve()
        except (OSError, ValueError, KeyError, StopIteration, TypeError) as error:
            raise BuildConfigurationError(
                f"Cannot resolve pinned WickedEngine default from {manifest_path}: {error}"
            ) from error
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
        "basisu_transcoder": ENGINE_ROOT / "dependencies/basisu/transcoder",
        "recast": ENGINE_ROOT / "dependencies/recast",
        "ozz": ENGINE_ROOT / "dependencies/ozz",
        "sdl_include": sdl_include,
        "sdl_library": sdl_library,
        "brew_include": brew_include,
        "brew_library": brew_library,
    }


def ozz_libraries(ozz: Path) -> list[Path]:
    """ozz-animation static libraries in link order (offline, runtime, base)."""
    return [ozz / f"build/src/{name}" for name in (
        "animation/offline/libozz_animation_offline_r.a", "animation/runtime/libozz_animation_r.a",
        "base/libozz_base_r.a")]


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
        paths["basisu_transcoder"] / "basisu_transcoder.h",
        paths["basisu_transcoder"] / "basisu_transcoder.cpp",
        paths["recast"] / "build/Recast/libRecast.a",
        paths["recast"] / "build/Detour/libDetour.a",
        *ozz_libraries(paths["ozz"]),
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


def default_native_compiler() -> str:
    # The checked-in SDL3 Wicked archives are built with Apple's libc++ ABI.
    # Prefer Apple's matching compiler even when Homebrew LLVM comes first on PATH.
    return "/usr/bin/clang++" if sys.platform == "darwin" else "clang++"


def installed_compiler_root(compiler: Path) -> Path | None:
    """Read the installer's literal launcher target without executing shell code."""
    if compiler.stat().st_size > 4096:
        return None
    try:
        lines = compiler.read_text(encoding="utf-8").splitlines()
        commands = [line for line in lines if line.strip() and not line.lstrip().startswith("#")]
        if len(commands) != 1:
            return None
        command = shlex.split(commands[0])
    except (UnicodeError, ValueError):
        return None
    if len(command) != 4 or command[:2] != ["exec", "bash"] or command[3] != "$@":
        return None
    launcher = Path(command[2])
    if (not launcher.is_absolute() or "$" in command[2] or "`" in command[2]
            or launcher.name != "elisac_stage1.sh" or launcher.parent.name != "scripts"
            or not launcher.is_file()):
        return None
    return launcher.parent.parent.resolve()


def resolve_runtime_object(argument: str | None, compiler: str) -> Path:
    configured = configured_path(argument, "ELISA_RUNTIME_OBJ")
    if configured is not None:
        if not configured.is_file():
            raise BuildConfigurationError(f"Elisa runtime object does not exist: {configured}")
        return configured

    compiler_file = Path(compiler).expanduser()
    if not compiler_file.is_file():
        located = shutil.which(compiler)
        if located is None:
            raise BuildConfigurationError(
                "Cannot locate the Elisa compiler to find its runtime object; "
                "set --runtime-object or ELISA_RUNTIME_OBJ"
            )
        compiler_file = Path(located)
    compiler_file = compiler_file.resolve()
    installed_root = installed_compiler_root(compiler_file)
    roots = ([installed_root] if installed_root is not None else
        [compiler_file.parent.parent, compiler_file.parent])
    for compiler_root in roots:
        candidate = compiler_root / "build/runtime/elisacore_runtime.o"
        if candidate.is_file():
            return candidate
    raise BuildConfigurationError(
        "Elisa runtime object was not found next to the compiler; "
        "set --runtime-object or ELISA_RUNTIME_OBJ"
    )
