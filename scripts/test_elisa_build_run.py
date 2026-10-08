#!/usr/bin/env python3
"""Focused CLI checks for the engine-owned Elisa build/run command."""

from __future__ import annotations

import json
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from elisa_build_run_test_support import (  # noqa: F401 - shared fixtures
    SCRIPT, assert_staged_output, fake_cook_command, fake_native_paths,
    mocked_asset_cooker, touch, write_fake_tools,
)
from elisa_build_run_asset_cook_cases import AssetCookTests  # noqa: F401 - collected here
from test_wicked_runtime_publication import RuntimePublicationTests  # noqa: F401 - collected here
from test_cook_publication import CookPublicationTests  # noqa: F401 - collected here


class BuildRunCliTests(unittest.TestCase):
    def test_output_collisions_preserve_author_files_before_compile(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa author paths ") as temporary:
            project = Path(temporary)
            source = project / "main.elisa"
            source.write_text("def main() -> i32:\n    0\n", encoding="utf-8")
            manifest = project / "elisa.project.json"
            compiler = project / "inert compiler"
            marker = project / "compiler invoked"
            compiler.write_text(f"#!{sys.executable}\nfrom pathlib import Path\n"
                f"Path({str(marker)!r}).write_text('invoked')\nraise SystemExit(42)\n", encoding="utf-8")
            compiler.chmod(0o755)
            symlink = project / "source symlink"
            symlink.symlink_to(source)
            hardlink = project / "source hardlink"
            os.link(source, hardlink)
            manifest_link = project / "manifest hardlink"
            manifest.write_text("{}", encoding="utf-8")
            os.link(manifest, manifest_link)
            for output in ("main.elisa", "./main.elisa", str(source), str(symlink),
                           str(hardlink), "elisa.project.json", str(manifest_link)):
                with self.subTest(output=output):
                    manifest.write_text(json.dumps({"main": "main.elisa", "host": "console",
                        "output": output}), encoding="utf-8")
                    before = {path: path.read_bytes() for path in (source, manifest)}
                    result = subprocess.run([sys.executable, str(SCRIPT), "build",
                        "--project", str(project), "--compiler", str(compiler)],
                        capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn("Output path would overwrite", result.stderr)
                    self.assertNotIn("Traceback", result.stderr)
                    self.assertFalse(marker.exists())
                    self.assertEqual(before, {path: path.read_bytes() for path in before})
            # A distinct output still reaches the selected compiler.
            manifest.write_text(json.dumps({"main": "main.elisa", "host": "console",
                "output": "build/game"}), encoding="utf-8")
            result = subprocess.run([sys.executable, str(SCRIPT), "build", "--project",
                str(project), "--compiler", str(compiler)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 42, result.stderr)
            self.assertTrue(marker.exists())

    def test_hosted_archive_optimization_and_runtime_ownership(self) -> None:
        runner = __import__("elisa_build_run")
        for optimize in (False, True):
            with self.subTest(optimize=optimize), mock.patch.object(runner, "run_command", return_value=0) as run:
                self.assertEqual(runner.compile_archive("compiler", Path("entry.elisa"),
                    Path("entry.a"), optimize=optimize), 0)
                arguments = run.call_args.args[0]
                self.assertEqual("-O2" in arguments, optimize)
                self.assertEqual(arguments[-5:], ["-emit", "c-archive", "-o", "entry.a", "entry.elisa"])
                self.assertEqual(run.call_args.kwargs["env"]["ELISA_RUNTIME_OBJ"], "none")

    def test_macos_default_native_compiler_matches_wicked_build(self) -> None:
        runner = __import__("elisa_build_run")
        with mock.patch.object(runner.sys, "platform", "darwin"):
            self.assertEqual(runner.default_native_compiler(), "/usr/bin/clang++")
        with mock.patch.object(runner.sys, "platform", "linux"):
            self.assertEqual(runner.default_native_compiler(), "clang++")

    def test_help_is_available(self) -> None:
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--help"], text=True, capture_output=True, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("build", result.stdout)
        self.assertIn("run", result.stdout)

    def test_project_manifest_sets_paths_and_runtime_settings(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa CLI project ") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "Wall Game project"
            main_source = project / "source files" / "main game.elisa"
            main_source.parent.mkdir(parents=True)
            main_source.write_text("using Application\ndef main() -> i32:\n    0\n", encoding="utf-8")
            output = project / "build output" / "wall game"
            (project / "elisa.project.json").write_text(json.dumps({
                "name": "wall-game",
                "main": str(main_source.relative_to(project)),
                "output": str(output.relative_to(project)),
                "application": {"title": "Wall Game Ω", "width": 1100, "height": 700, "hidden": True},
            }), encoding="utf-8")
            wicked_root, wicked_build, sdl_root, brew_root = fake_native_paths(root)
            compiler, linker, log_dir = write_fake_tools(root)
            runtime_object = root / "Elisa runtime with spaces" / "elisacore_runtime.o"
            touch(runtime_object)
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "ELISA_RUNTIME_OBJ": str(runtime_object),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
            }
            runner = __import__("elisa_build_run")
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"), \
                    mock.patch.object(runner, "validate_wicked_archive_abi", return_value=0) as abi_check:
                status = runner.main([
                    "run", "--project", str(project),
                ])

            self.assertEqual(status, 0)
            self.assertTrue(abi_check.called)
            self.assertTrue(output.is_file())
            self.assertEqual((output.parent / "libdxcompiler.dylib").resolve(),
                (wicked_root / "WickedEngine/libdxcompiler.dylib").resolve())
            provenance_path = output.with_name(output.name + ".provenance.json")
            self.assertTrue(provenance_path.is_file())
            provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
            self.assertEqual(provenance["binary"]["sha256"],
                hashlib.sha256(output.read_bytes()).hexdigest())
            self.assertEqual(provenance["main_source_sha256"],
                hashlib.sha256(main_source.read_bytes()).hexdigest())
            run_info = json.loads((log_dir / "ran.json").read_text())
            self.assertEqual(run_info["cwd"], str(project.resolve()))
            self.assertEqual(run_info["settings"], {
                "ELISA_PROJECT_TITLE": "Wall Game Ω",
                "ELISA_PROJECT_WIDTH": "1100",
                "ELISA_PROJECT_HEIGHT": "700",
                "ELISA_PROJECT_HIDDEN": "1",
                "ELISA_PROJECT_ROOT": str(project.resolve()),
            })
            compiler_args = json.loads((log_dir / "compiler.json").read_text())
            wrapper_path = Path(compiler_args[-1])
            self.assertIn(" ", str(wrapper_path))
            wrapper_text = (log_dir / "wrapper.txt").read_text()
            self.assertIn(f'include "{SCRIPT.parent.parent / "src/runtime/public.elisa"}"', wrapper_text)
            self.assertIn(f'include "{main_source.resolve()}"', wrapper_text)
            linker_args = json.loads((log_dir / "linker.json").read_text())
            output_index = linker_args.index("-o") + 1
            self.assertIn(" ", linker_args[output_index])
            self.assertIn(" ", str(output))
        self.assertIn(str(SCRIPT.parent.parent / "native/render_scene_abi.cpp"), linker_args)
        self.assertIn(str(SCRIPT.parent.parent / "dependencies/basisu/transcoder/basisu_transcoder.cpp"), linker_args)
        self.assertIn(str((wicked_root / "WickedEngine/Utility/DirectXMath").resolve()), linker_args)
        self.assertIn(str(runtime_object.resolve()), linker_args)
        self.assertIn("-fno-rtti", linker_args)

    def test_wicked_runtime_staging_is_idempotent_and_tracks_selected_checkout(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Wicked runtime staging ") as temporary_directory:
            root = Path(temporary_directory)
            executable = root / "game-build/game"
            touch(executable)
            first_source = root / "first checkout/WickedEngine"
            second_source = root / "second checkout/WickedEngine"
            touch(first_source / "libdxcompiler.dylib")
            touch(first_source / "libmetalirconverter.dylib")
            touch(second_source / "libdxcompiler.dylib")

            staged = runner.stage_wicked_runtime_libraries(executable, first_source)
            self.assertEqual(len(staged), 2)
            self.assertTrue(all(path.is_symlink() for path in staged))
            self.assertEqual(runner.stage_wicked_runtime_libraries(executable, first_source), [])

            runner.stage_wicked_runtime_libraries(executable, second_source)
            self.assertEqual((executable.parent / "libdxcompiler.dylib").resolve(),
                (second_source / "libdxcompiler.dylib").resolve())
            self.assertFalse((executable.parent / "libmetalirconverter.dylib").exists())

    def test_wicked_runtime_staging_rejects_unmanaged_collision(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Wicked runtime collision ") as temporary_directory:
            root = Path(temporary_directory)
            executable = root / "build/game"
            source = root / "WickedEngine"
            touch(executable)
            touch(source / "libdxcompiler.dylib")
            collision = executable.parent / "libdxcompiler.dylib"
            touch(collision)
            with self.assertRaisesRegex(runner.WickedRuntimeError, "refusing to replace"):
                runner.stage_wicked_runtime_libraries(executable, source)

    def test_wicked_runtime_staging_requires_dxc(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Wicked runtime missing DXC ") as temporary_directory:
            root = Path(temporary_directory)
            executable = root / "build/game"
            touch(executable)
            (root / "WickedEngine").mkdir()
            with self.assertRaisesRegex(runner.WickedRuntimeError, "shader compiler is missing"):
                runner.stage_wicked_runtime_libraries(executable, root / "WickedEngine")

    def test_wicked_runtime_staging_rejects_orphaned_converter_file(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Wicked runtime orphan ") as temporary_directory:
            root = Path(temporary_directory)
            executable = root / "build/game"
            source = root / "WickedEngine"
            touch(executable)
            touch(source / "libdxcompiler.dylib")
            touch(executable.parent / "libmetalirconverter.dylib")
            with self.assertRaisesRegex(runner.WickedRuntimeError, "is absent from this Wicked checkout"):
                runner.stage_wicked_runtime_libraries(executable, source)

    def test_runtime_object_is_discovered_beside_compiler(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Elisa compiler runtime ") as temporary_directory:
            root = Path(temporary_directory)
            compiler = root / "bin/elisac-stage1"
            runtime_object = root / "build/runtime/elisacore_runtime.o"
            touch(compiler)
            touch(runtime_object)
            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(runner.resolve_runtime_object(None, str(compiler)), runtime_object.resolve())

    def test_wicked_abi_guard_checks_every_linked_archive(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa Wicked ABI guard ") as temporary_directory:
            root = Path(temporary_directory)
            _, wicked_build, _, _ = fake_native_paths(root)
            runner = __import__("elisa_build_run")
            paths = {"libraries": wicked_build / "WickedEngine"}
            with mock.patch.object(runner, "run_command", return_value=0) as run:
                self.assertEqual(runner.validate_wicked_archive_abi(paths, "clang++"), 0)

            command = run.call_args.args[0]
            utility = paths["libraries"] / "Utility"
            self.assertEqual(command[0], sys.executable)
            self.assertEqual(command[1], str(SCRIPT.parent / "check_wicked_archive_abi.py"))
            self.assertEqual(command[2:4], ["--compiler", "clang++"])
            self.assertEqual(command[4:], [str(path) for path in (
                paths["libraries"] / "libWickedEngine.a",
                paths["libraries"] / "libJolt.a",
                utility / "libUtility.a",
                utility / "FAudio/libFAudio.a",
                paths["libraries"] / "LUA/libLUA.a",
            )])

    def test_mixed_wicked_abi_stops_before_compile_and_link(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa mixed Wicked ABI ") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "project"
            project.mkdir()
            (project / "main.elisa").write_text("def main() -> i32:\n    0\n", encoding="utf-8")
            output = project / "game"
            wicked_root, wicked_build, sdl_root, brew_root = fake_native_paths(root)
            compiler, linker, log_dir = write_fake_tools(root)
            runtime_object = root / "runtime/elisacore_runtime.o"
            touch(runtime_object)
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "ELISA_RUNTIME_OBJ": str(runtime_object),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
            }
            runner = __import__("elisa_build_run")
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"), \
                    mock.patch.object(runner, "validate_wicked_archive_abi", return_value=1):
                status = runner.main([
                    "build", "--project", str(project), "--main", "main.elisa", "--output", str(output),
                ])

            self.assertEqual(status, 1)
            self.assertFalse((log_dir / "compiler.json").exists())
            self.assertFalse((log_dir / "linker.json").exists())
            self.assertFalse(output.exists())
            self.assertFalse(output.with_name(output.name + ".provenance.json").exists())

    def test_native_link_optimizes_only_on_request(self) -> None:
        runner = __import__("elisa_build_run")
        paths = {key: Path("/opt/fake") for key in (
            "wicked_source", "libraries", "sdl_include", "sdl_library", "brew_include",
            "brew_library", "miniaudio_include", "basisu_transcoder", "recast", "ozz")}
        arguments = (Path("/tmp/entry.a"), Path("/tmp/application"), Path("/tmp/build"), paths)
        default = runner.native_link_command("clang++", *arguments)
        optimized = runner.native_link_command("clang++", *arguments, optimize=True)
        identified = runner.native_link_command("clang++", *arguments, build_identity=0x12345)
        self.assertIn("-O0", default)
        self.assertNotIn("-O2", default)
        self.assertIn("-O2", optimized)
        self.assertNotIn("-O0", optimized)
        self.assertIn("-DELISA_APPLICATION_BUILD_ID=74565", identified)

    def test_command_line_paths_override_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa manifest overrides ") as temporary_directory:
            project = Path(temporary_directory) / "project"
            project.mkdir()
            (project / "manifest.elisa").write_text("def main() -> i32:\n    0\n", encoding="utf-8")
            (project / "override.elisa").write_text("def main() -> i32:\n    0\n", encoding="utf-8")
            (project / "elisa.project.json").write_text(json.dumps({
                "name": "configured-game", "main": "manifest.elisa",
            }), encoding="utf-8")
            runner = __import__("elisa_build_run")
            manifest_args = runner.parse_arguments(["build", "--project", str(project)])
            _, manifest_main, manifest_output = runner.resolve_project_paths(manifest_args)
            self.assertEqual(manifest_main, (project / "manifest.elisa").resolve())
            self.assertEqual(manifest_output, (project / "build/configured-game").resolve())
            args = runner.parse_arguments([
                "build", "--project", str(project), "--main", "override.elisa", "--output", "override-output",
            ])
            resolved_project, main_source, output = runner.resolve_project_paths(args)
            self.assertEqual(resolved_project, project.resolve())
            self.assertEqual(main_source, (project / "override.elisa").resolve())
            self.assertEqual(output, (project / "override-output").resolve())

    def test_invalid_project_application_settings_are_rejected(self) -> None:
        runner = __import__("elisa_build_run")
        for application in (
            {"width": True},
            {"height": 0},
            {"hidden": 1},
            {"title": "bad\0title"},
            {"title": "x" * 256},
        ):
            with self.subTest(application=application), self.assertRaises(runner.BuildConfigurationError):
                runner.application_settings({"application": application})

    def test_game_owned_exports_are_rejected_before_native_link(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa ABI audit ") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "project"
            project.mkdir()
            main_source = project / "main.elisa"
            main_source.write_text("def main() -> i32:\n    0\n", encoding="utf-8")
            output = project / "game"
            wicked_root, wicked_build, sdl_root, brew_root = fake_native_paths(root)
            compiler, linker, log_dir = write_fake_tools(root)
            runtime_object = root / "runtime/elisacore_runtime.o"
            touch(runtime_object)
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "ELISA_RUNTIME_OBJ": str(runtime_object),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
                "FAKE_EXPORT": "1",
            }
            runner = __import__("elisa_build_run")
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"), \
                    mock.patch.object(runner, "validate_wicked_archive_abi", return_value=0):
                status = runner.main([
                    "build", "--project", str(project), "--main", "main.elisa", "--output", str(output),
                ])
            self.assertEqual(status, 2)
            self.assertFalse((log_dir / "linker.json").exists())
            self.assertFalse(output.exists())

    def test_failed_compile_does_not_run_stale_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa CLI failure ") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "project"
            project.mkdir()
            main_source = project / "main.elisa"
            main_source.write_text("using Application\ndef main() -> i32:\n    0\n", encoding="utf-8")
            assets = project / "assets"
            assets.mkdir()
            (assets / "walk.fbx").write_bytes(b"fbx")
            (project / "elisa.project.json").write_text(json.dumps({"asset_cooks": [{
                "source": "assets/walk.fbx", "asset_path": "assets/walk.fbx",
                "output": "build/walk.pkg",
            }]}), encoding="utf-8")
            output = project / "old game"
            output.write_text("stale", encoding="utf-8")
            wicked_root, wicked_build, sdl_root, brew_root = fake_native_paths(root)
            compiler, linker, log_dir = write_fake_tools(root)
            runtime_object = root / "runtime/elisacore_runtime.o"
            touch(runtime_object)
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "ELISA_RUNTIME_OBJ": str(runtime_object),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
                "FAKE_COMPILER_STATUS": "17",
            }
            runner = __import__("elisa_build_run")
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"), \
                    mock.patch.object(runner, "validate_wicked_archive_abi", return_value=0):
                status = runner.main([
                    "run", "--project", str(project), "--main", "main.elisa", "--output", str(output),
                ])
            self.assertEqual(status, 17)
            self.assertEqual(output.read_text(), "stale")
            self.assertFalse(output.with_name(output.name + ".provenance.json").exists())
            self.assertFalse((log_dir / "linker.json").exists())
            self.assertFalse((log_dir / "ran.json").exists())
            self.assertFalse((project / "build/walk.pkg").exists(),
                "asset conversion should not run after Elisa compilation fails")
            self.assertFalse((project / "build/.elisa-asset-cook-cache.json").exists())

    def test_console_build_preserves_previous_executable_on_failure(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Elisa console publication ") as temporary:
            project = Path(temporary)
            source = project / "main.elisa"
            source.write_text("def main() -> i32:\n    0\n")
            output = project / "build/tool"
            output.parent.mkdir()
            previous = b"previous executable"
            output.write_bytes(previous)
            output.chmod(0o755)
            (project / "elisa.project.json").write_text(json.dumps({
                "host": "console", "main": "main.elisa", "output": "build/tool"}))
            compiler = project / "compiler"
            compiler.write_text(f"#!{sys.executable}\nimport os, pathlib, sys\n"
                "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
                "mode = os.environ['CONSOLE_CONTROL']\n"
                "if mode != 'missing': output.write_bytes(b'new executable')\n"
                "if mode == 'failed': raise SystemExit(42)\n"
                "if mode != 'missing': output.chmod(0o755)\n")
            compiler.chmod(0o755)
            for mode, expected in (("failed", 42), ("missing", 1), ("success", 0)):
                with self.subTest(mode=mode), mock.patch.dict(os.environ, {"CONSOLE_CONTROL": mode}):
                    status = runner.main(["build", "--project", str(project), "--compiler", str(compiler)])
                    self.assertEqual(status, expected)
                    self.assertEqual(output.read_bytes(), b"new executable" if mode == "success" else previous)
                    self.assertEqual(output.stat().st_mode & 0o777, 0o755)
                    self.assertEqual(list(output.parent.iterdir()), [output])

    def test_console_host_compiles_executable_without_native_host(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa console ") as temporary_directory:
            root = Path(temporary_directory)
            project = root / "tool project"
            (project / "src").mkdir(parents=True)
            (project / "src/main.elisa").write_text("def main() -> i32:\n    0\n", encoding="utf-8")
            (project / "elisa.project.json").write_text(json.dumps({
                "name": "Tool", "main": "src/main.elisa", "output": "build/tool", "host": "console",
            }), encoding="utf-8")
            log_dir = root / "logs"
            log_dir.mkdir()
            compiler = root / "fake compiler"
            compiler.write_text(
                "#!/usr/bin/env python3\n"
                "import json, os, pathlib, sys\n"
                "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'compiler.json').write_text(json.dumps(sys.argv[1:]))\n"
                "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
                "out.write_text('#!/usr/bin/env python3\\nimport json, os, sys\\n'\n"
                "    'open(os.path.join(os.environ[\"FAKE_LOG_DIR\"], \"ran.json\"), \"w\").write(json.dumps(sys.argv[1:]))\\n')\n"
                "out.chmod(0o755)\n",
                encoding="utf-8",
            )
            compiler.chmod(0o755)
            runner = __import__("elisa_build_run")
            environment = {"ELISA_COMPILER_BIN": str(compiler), "FAKE_LOG_DIR": str(log_dir),
                "PATH": os.environ.get("PATH", "")}
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "linux"), \
                    mock.patch.object(runner, "resolve_native_paths", side_effect=AssertionError("native host used")):
                status = runner.main(["run", "--project", str(project), "--", "clean", "in.glb"])
            self.assertEqual(status, 0)
            arguments = json.loads((log_dir / "compiler.json").read_text())
            self.assertEqual(arguments[:2], ["-emit", "exe"])
            self.assertEqual(arguments[-1], str((project / "src/main.elisa").resolve()))
            self.assertEqual(json.loads((log_dir / "ran.json").read_text()), ["clean", "in.glb"])
            self.assertTrue((project / "build/tool").is_file())

    def test_unknown_project_host_is_rejected(self) -> None:
        runner = __import__("elisa_build_run")
        with self.assertRaises(runner.BuildConfigurationError):
            runner.project_host({"host": "web"}, False)
        self.assertEqual(runner.project_host({}, True), "console")
        self.assertEqual(runner.project_host({}, False), "application")


if __name__ == "__main__":
    unittest.main()
