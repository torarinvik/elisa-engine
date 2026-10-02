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


class BuildRunCliTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
