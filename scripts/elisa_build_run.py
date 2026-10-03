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
import time
from pathlib import Path

import asset_cooks
from asset_cooks import BuildConfigurationError
from build_provenance import compute_build_identity, write_build_provenance
from native_toolchain import (  # noqa: F401 - re-exported for callers and tests
    ENGINE_ROOT, brew_prefix, configured_path, default_native_compiler,
    installed_compiler_root, ozz_libraries, required_native_files,
    resolve_native_paths, resolve_runtime_object, validate_native_files,
)


PROJECT_MANIFEST = "elisa.project.json"
FRAMEWORKS = [
    "Foundation", "CoreFoundation", "CoreGraphics", "CoreText", "ImageIO",
    "Metal", "QuartzCore", "AppKit", "IOKit", "GameController", "AudioToolbox",
    "CoreAudio", "AVFoundation", "VideoToolbox", "Cocoa",
]


def validate_wicked_archive_abi(paths: dict[str, Path], compiler: str) -> int:
    libraries = paths["libraries"]
    utility = libraries / "Utility"
    archives = [
        libraries / "libWickedEngine.a",
        libraries / "libJolt.a",
        utility / "libUtility.a",
        utility / "FAudio/libFAudio.a",
        libraries / "LUA/libLUA.a",
    ]
    checker = ENGINE_ROOT / "scripts/check_wicked_archive_abi.py"
    return run_command([sys.executable, str(checker), "--compiler", compiler,
        *(str(path) for path in archives)])


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
            help="compile the native runtime with -O2 (or set ELISA_NATIVE_OPTIMIZE=1)")
        command.add_argument("--console", action="store_true",
            help="build a headless console executable without the Application host "
                 "(or set \"host\": \"console\" in the manifest)")
        if action == "run":
            command.add_argument("program_args", nargs=argparse.REMAINDER,
                help="arguments after -- are passed to the program")
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


def cook_declared_assets(project: Path, config: dict[str, object], *, force: bool = False) -> int:
    return asset_cooks.cook_declared_assets(project, config, run_command, force=force)


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


def write_entry_wrapper(destination: Path, main_source: Path,
    include_public_runtime: bool = True) -> None:
    runtime_bundle = ENGINE_ROOT / "src/runtime/public.elisa"
    includes = ((runtime_bundle, main_source) if include_public_runtime else (main_source,))
    for path in includes:
        if any(character in str(path) for character in ('"', "\n", "\r")):
            raise BuildConfigurationError(f"Elisa include path contains an unsupported quote or newline: {path}")
    destination.write_text("".join(f'include "{path}"\n' for path in includes),
        encoding="utf-8")


def compile_archive(compiler: str, wrapper: Path, archive: Path) -> int:
    # The engine linker adds the compiler's runtime object as a separate input.
    # Prevent `-emit c-archive` from bundling that same object into the app archive;
    # current compiler builds include global source metadata there, so linking both
    # copies produces duplicate symbols.
    archive_env = dict(os.environ)
    archive_env["ELISA_RUNTIME_OBJ"] = "none"
    return run_command([compiler, "-emit", "c-archive", "-o", str(archive), str(wrapper)],
        env=archive_env)


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


def runtime_object_for_link(runtime_object: Path, archive: Path, build_dir: Path) -> Path:
    """Remove only duplicated compiler LSP labels from a staged runtime copy."""
    if runtime_object.stat().st_size == 0:
        return runtime_object
    runtime_symbols = defined_global_symbols(runtime_object)
    archive_symbols = defined_global_symbols(archive)
    lsp_prefix = "___lsp_decl_name"
    duplicates = sorted(symbol for symbol in runtime_symbols & archive_symbols
        if symbol == lsp_prefix or symbol.startswith(lsp_prefix + "."))
    if not duplicates:
        return runtime_object

    nmedit = shutil.which("nmedit") or "/usr/bin/nmedit"
    if not Path(nmedit).is_file():
        raise BuildConfigurationError(
            "The Elisa compiler emitted duplicate LSP declaration labels in the runtime and "
            "application archive, and macOS nmedit is unavailable to remove them."
        )
    staged_runtime = build_dir / "elisacore_runtime_link.o"
    symbol_list = build_dir / "elisacore_runtime_duplicate_symbols.txt"
    shutil.copyfile(runtime_object, staged_runtime)
    symbol_list.write_text("".join(f"{symbol}\n" for symbol in duplicates), encoding="utf-8")
    result = subprocess.run([nmedit, "-R", str(symbol_list), str(staged_runtime)],
        text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise BuildConfigurationError(
            f"Could not remove duplicate Elisa LSP labels from the staged runtime: {result.stderr.strip()}"
        )
    remaining = set(duplicates) & defined_global_symbols(staged_runtime)
    if remaining:
        raise BuildConfigurationError(
            "Duplicate Elisa LSP labels remain after staging the runtime object: "
            + ", ".join(sorted(remaining))
        )
    return staged_runtime


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
    build_dir: Path, paths: dict[str, Path], native_test_probes: bool = False,
    optimize: bool = False, runtime_object: Path | None = None,
    build_identity: int = 0) -> list[str]:
    wicked_source = paths["wicked_source"]
    libraries = paths["libraries"]
    utility = libraries / "Utility"
    utility_source = wicked_source / "Utility"
    sdl_include = paths["sdl_include"]
    sdl_library = paths["sdl_library"]
    brew_include = paths["brew_include"]
    brew_library = paths["brew_library"]
    # Engine bridges use no RTTI and must also link against RTTI-disabled Wicked.
    command = [
        cxx, "-std=c++17", "-O2" if optimize else "-O0", "-fno-rtti", "-include", "filesystem",
        "-DWI_UNORDERED_MAP_TYPE=2",
        "-DWICKED_CMAKE_BUILD", "-DSDL3=1", "-D__OBJC_BOOL_IS_BOOL=1",
        f"-DELISA_APPLICATION_BUILD_ID={build_identity}",
        "-I", str(build_dir), "-I", str(ENGINE_ROOT / "native"),
        "-I", str(ENGINE_ROOT / "dependencies/meshoptimizer"), "-I", str(wicked_source),
        "-I", str(utility_source), "-I", str(utility_source / "metal"),
        "-I", str(utility_source / "DirectXMath"),
        "-I", str(sdl_include), "-I", str(sdl_include / "SDL3"),
        "-I", str(brew_include), "-I", str(brew_include / "freetype2"),
        "-I", str(brew_include / "harfbuzz"),
        "-I", str(paths["miniaudio_include"]),
        "-I", str(paths["basisu_transcoder"]),
        "-I", str(paths["recast"] / "Recast/Include"), "-I", str(paths["recast"] / "Detour/Include"),
        "-I", str(paths["ozz"] / "include"),
        str(ENGINE_ROOT / "native/application_abi.cpp"),
        str(ENGINE_ROOT / "native/application_test_probe.cpp"),
        str(ENGINE_ROOT / "native/render_scene_abi.cpp"),
        str(ENGINE_ROOT / "native/meshopt_stream_codec.cpp"),
        str(ENGINE_ROOT / "dependencies/meshoptimizer/indexcodec.cpp"),
        str(ENGINE_ROOT / "dependencies/meshoptimizer/vertexcodec.cpp"),
        str(ENGINE_ROOT / "native/elisa_native_fallbacks.cpp"),
        str(ENGINE_ROOT / "native/audio_service_abi.cpp"),
        str(ENGINE_ROOT / "native/miniaudio_implementation.cpp"),
        str(ENGINE_ROOT / "native/physics_service_abi.cpp"),
        str(ENGINE_ROOT / "native/physics_shape_service_abi.cpp"),
        str(ENGINE_ROOT / "native/user_data_abi.cpp"),
        str(ENGINE_ROOT / "native/navigation_service_abi.cpp"),
        str(paths["basisu_transcoder"] / "basisu_transcoder.cpp"),
        str(wicked_source / "wiAppleHelper.mm"), str(wicked_source / "wiInput_Apple.mm"),
        str(archive),
    ]
    if runtime_object is not None:
        command.append(str(runtime_object))
    command.extend([
        str(libraries / "libWickedEngine.a"), str(libraries / "libJolt.a"),
        str(utility / "libUtility.a"), str(utility / "FAudio/libFAudio.a"),
        str(libraries / "LUA/libLUA.a"),
        str(paths["recast"] / "build/Recast/libRecast.a"),
        str(paths["recast"] / "build/Detour/libDetour.a"),
        *map(str, ozz_libraries(paths["ozz"])),
        "-L", str(sdl_library), "-lSDL3", "-L", str(brew_library),
        "-lfreetype", "-lharfbuzz", "-lzstd", "-Wl,-rpath,@executable_path",
        "-Wl,-rpath," + str(wicked_source),
    ])
    if native_test_probes:
        command.extend(["-DELISA_APPLICATION_TEST_PROBE=1", "-DELISA_AUDIO_TEST_PROBE=1", "-DELISA_PHYSICS_TEST_PROBE=1", "-DELISA_RENDER_SCENE_TEST_PROBE=1"])
    for framework in FRAMEWORKS:
        command.extend(["-framework", framework])
    command.extend(["-o", str(staged_output)])
    return command


def project_host(config: dict[str, object], console_flag: bool) -> str:
    host = config.get("host", "application")
    if host not in ("application", "console"):
        raise BuildConfigurationError("project 'host' must be \"application\" or \"console\"")
    return "console" if console_flag else str(host)


def build_console(args: argparse.Namespace, main_source: Path,
    output: Path) -> tuple[int, Path | None, Path | None]:
    """Headless tools: compile main() straight to an executable, no native host."""
    compiler = args.compiler or os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    command = [compiler]
    if args.optimize or os.environ.get("ELISA_NATIVE_OPTIMIZE") == "1":
        command.append("-O2")
    command.extend(["-emit", "exe", "-o", str(output), str(main_source)])
    status = run_command(command)
    if status != 0:
        return status, None, None
    if not output.is_file():
        print("Elisa compiler succeeded without creating the output executable.", file=sys.stderr)
        return 1, None, None
    return 0, output, None


def build_project(args: argparse.Namespace) -> tuple[int, Path | None, Path | None]:
    project, main_source, output = resolve_project_paths(args)
    config = load_project_config(project)
    if project_host(config, getattr(args, "console", False)) == "console":
        status = cook_declared_assets(project, config)
        if status != 0:
            return status, None, None
        return build_console(args, main_source, output)
    if sys.platform != "darwin":
        print("The SDL3/Metal Application host currently supports macOS only.", file=sys.stderr)
        return 2, None, None
    paths = resolve_native_paths(args)
    validate_native_files(paths)
    cxx = args.cxx or os.environ.get("CXX", default_native_compiler())
    compiler = args.compiler or os.environ.get("ELISA_COMPILER_BIN", "elisac-stage1")
    runtime_object = resolve_runtime_object(args.runtime_object, compiler)
    library_dir = paths["libraries"]
    utility_dir = library_dir / "Utility"
    brew_link_inputs = [
        next((paths["brew_library"] / f"lib{name}.{suffix}"
            for suffix in ("dylib", "a")
            if (paths["brew_library"] / f"lib{name}.{suffix}").is_file()),
            paths["brew_library"] / f"lib{name}.dylib")
        for name in ("freetype", "harfbuzz", "zstd")]
    native_artifacts = [
        library_dir / "libWickedEngine.a", library_dir / "libJolt.a",
        utility_dir / "libUtility.a", utility_dir / "FAudio/libFAudio.a",
        library_dir / "LUA/libLUA.a", paths["recast"] / "build/Recast/libRecast.a",
        paths["recast"] / "build/Detour/libDetour.a",
        *ozz_libraries(paths["ozz"]),
        paths["sdl_library"] / "libSDL3.dylib",
        *brew_link_inputs,
        runtime_object,
    ]
    build_options = {
        "action": args.action,
        "native_optimize": args.optimize or os.environ.get("ELISA_NATIVE_OPTIMIZE") == "1",
        "public_runtime": not args.no_public_runtime,
        "native_test_probes": args.native_test_probes,
        "force_cook_assets": args.force_cook_assets,
        "wicked_build": str(paths["wicked_build"]),
        "compiler_request": compiler,
        "native_compiler_request": cxx,
    }
    abi_status = validate_wicked_archive_abi(paths, cxx)
    if abi_status != 0:
        return abi_status, None, None
    build_identity = compute_build_identity(project=project, main_source=main_source,
        engine_root=ENGINE_ROOT, wicked_root=paths["wicked_root"], compiler=compiler,
        cxx=cxx, runtime_object=runtime_object, native_artifacts=native_artifacts,
        options=build_options, output=output)
    with tempfile.TemporaryDirectory(prefix="Elisa application build ", dir=output.parent) as temporary_directory:
        build_dir = Path(temporary_directory)
        wrapper = build_dir / "application_entry.elisa"
        archive = build_dir / "application_entry.a"
        staged_output = build_dir / "application"
        write_entry_wrapper(wrapper, main_source,
            include_public_runtime=not args.no_public_runtime)
        stage_started = time.perf_counter()
        status = compile_archive(compiler, wrapper, archive)
        print(f"Elisa archive compile: {time.perf_counter() - stage_started:.2f}s", flush=True)
        if status != 0:
            return status, None, None
        audit_archive(archive)
        stage_started = time.perf_counter()
        status = cook_declared_assets(project, config, force=args.force_cook_assets)
        print(f"Asset cooking stage: {time.perf_counter() - stage_started:.2f}s", flush=True)
        if status != 0:
            return status, None, None
        command = native_link_command(cxx, archive, staged_output, build_dir, paths,
            args.native_test_probes, args.optimize or os.environ.get("ELISA_NATIVE_OPTIMIZE") == "1",
            runtime_object_for_link(runtime_object, archive, build_dir), build_identity)
        stage_started = time.perf_counter()
        status = run_command(command, cwd=project)
        print(f"Native link: {time.perf_counter() - stage_started:.2f}s", flush=True)
        if status != 0:
            return status, None, None
        if not staged_output.is_file():
            print("Native linker succeeded without creating the output executable.", file=sys.stderr)
            return 1, None, None
        output.parent.mkdir(parents=True, exist_ok=True)
        os.replace(staged_output, output)
    try:
        provenance = write_build_provenance(output=output, project=project,
            main_source=main_source, engine_root=ENGINE_ROOT,
            wicked_root=paths["wicked_root"], compiler=compiler, cxx=cxx,
            runtime_object=runtime_object, native_artifacts=native_artifacts,
            options=build_options, build_identity=build_identity)
        print(f"Build provenance: {provenance}", flush=True)
    except (OSError, ValueError) as error:
        print(f"Could not write build provenance: {error}", file=sys.stderr, flush=True)
    print(f"Application build complete: {output}", flush=True)
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
        project = Path(args.project).expanduser().resolve()
        program_args = [a for a in (args.program_args or [])]
        if program_args[:1] == ["--"]:
            program_args = program_args[1:]
        if shader_path is None:
            return run_command([str(output), *program_args], cwd=project, env=runtime_env)
        runtime_env["ELISA_ENGINE_SHADER_PATH"] = str(shader_path)
        app = application_settings(load_project_config(project))
        runtime_env["ELISA_PROJECT_TITLE"] = str(app["title"])
        runtime_env["ELISA_PROJECT_WIDTH"] = str(app["width"])
        runtime_env["ELISA_PROJECT_HEIGHT"] = str(app["height"])
        runtime_env["ELISA_PROJECT_HIDDEN"] = "1" if app["hidden"] else "0"
        runtime_env["ELISA_PROJECT_ROOT"] = str(project)
        return run_command([str(output), *program_args], cwd=project, env=runtime_env)
    except BuildConfigurationError as error:
        print(f"elisa-build-run: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
