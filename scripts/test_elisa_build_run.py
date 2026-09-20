#!/usr/bin/env python3
"""Focused CLI checks for the engine-owned Elisa build/run command."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().with_name("elisa_build_run.py")


def touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()


def fake_native_paths(root: Path) -> tuple[Path, Path, Path, Path]:
    wicked_root = root / "Wicked checkout with spaces"
    wicked_source = wicked_root / "WickedEngine"
    wicked_build = root / "Wicked build with spaces"
    libraries = wicked_build / "WickedEngine"
    for relative in (
        "wiApplication.h", "wiAppleHelper.mm", "wiInput_Apple.mm",
        "libdxcompiler.dylib",
    ):
        touch(wicked_source / relative)
    (wicked_source / "shaders").mkdir(parents=True)
    for relative in (
        "libWickedEngine.a", "libJolt.a", "Utility/libUtility.a",
        "Utility/FAudio/libFAudio.a", "LUA/libLUA.a",
    ):
        touch(libraries / relative)

    sdl_root = root / "SDL3 SDK with spaces"
    touch(sdl_root / "include/SDL3/SDL.h")
    touch(sdl_root / "lib/libSDL3.dylib")
    brew_root = root / "Package prefix with spaces"
    touch(brew_root / "include/freetype2/ft2build.h")
    touch(brew_root / "include/harfbuzz/hb.h")
    for name in ("freetype", "harfbuzz", "zstd"):
        touch(brew_root / f"lib/lib{name}.dylib")
    return wicked_root, wicked_build, sdl_root, brew_root


def write_fake_tools(root: Path) -> tuple[Path, Path, Path]:
    log_dir = root / "fake tool logs"
    log_dir.mkdir()
    compiler = root / "fake Elisa compiler"
    compiler.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'compiler.json').write_text(json.dumps(sys.argv[1:]))\n"
        "if os.environ.get('FAKE_COMPILER_STATUS'): raise SystemExit(int(os.environ['FAKE_COMPILER_STATUS']))\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "output.write_bytes(b'fake archive')\n"
        "wrapper = pathlib.Path(sys.argv[-1])\n"
        "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'wrapper.txt').write_text(wrapper.read_text())\n"
        "exports = ['fake_export'] if os.environ.get('FAKE_EXPORT') else []\n"
        "manifest = {'exported_functions': exports, 'exported_globals': [], 'exported_types': []}\n"
        "output.with_suffix('.elisa-abi.json').write_text(json.dumps(manifest))\n",
        encoding="utf-8",
    )
    linker = root / "fake native linker"
    linker.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "pathlib.Path(os.environ['FAKE_LOG_DIR'], 'linker.json').write_text(json.dumps(sys.argv[1:]))\n"
        "output = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "program = '#!/usr/bin/env python3\\nimport json, os\\nfrom pathlib import Path\\n'\n"
        "program += 'settings = {key: os.environ.get(key) for key in (\"ELISA_PROJECT_TITLE\", \"ELISA_PROJECT_WIDTH\", \"ELISA_PROJECT_HEIGHT\", \"ELISA_PROJECT_HIDDEN\")}\\n'\n"
        "program += 'Path(os.environ[\"FAKE_LOG_DIR\"], \"ran.json\").write_text(json.dumps({\"cwd\": os.getcwd(), \"settings\": settings}))\\n'\n"
        "output.write_text(program)\n"
        "output.chmod(0o755)\n",
        encoding="utf-8",
    )
    compiler.chmod(0o755)
    linker.chmod(0o755)
    return compiler, linker, log_dir


class BuildRunCliTests(unittest.TestCase):
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
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
            }
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"):
                status = __import__("elisa_build_run").main([
                    "run", "--project", str(project),
                ])

            self.assertEqual(status, 0)
            self.assertTrue(output.is_file())
            run_info = json.loads((log_dir / "ran.json").read_text())
            self.assertEqual(run_info["cwd"], str(project.resolve()))
            self.assertEqual(run_info["settings"], {
                "ELISA_PROJECT_TITLE": "Wall Game Ω",
                "ELISA_PROJECT_WIDTH": "1100",
                "ELISA_PROJECT_HEIGHT": "700",
                "ELISA_PROJECT_HIDDEN": "1",
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
            self.assertIn(str((wicked_root / "WickedEngine/Utility/DirectXMath").resolve()), linker_args)

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
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
                "FAKE_EXPORT": "1",
            }
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"):
                status = __import__("elisa_build_run").main([
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
            output = project / "old game"
            output.write_text("stale", encoding="utf-8")
            wicked_root, wicked_build, sdl_root, brew_root = fake_native_paths(root)
            compiler, linker, log_dir = write_fake_tools(root)
            environment = {
                "WICKED_ROOT": str(wicked_root),
                "WICKED_BUILD": str(wicked_build),
                "WICKED_SDL3_ROOT": str(sdl_root),
                "WICKED_BREW_PREFIX": str(brew_root),
                "ELISA_COMPILER_BIN": str(compiler),
                "CXX": str(linker),
                "FAKE_LOG_DIR": str(log_dir),
                "FAKE_COMPILER_STATUS": "17",
            }
            with mock.patch.dict(os.environ, environment, clear=True), \
                    mock.patch.object(sys, "platform", "darwin"):
                status = __import__("elisa_build_run").main([
                    "run", "--project", str(project), "--main", "main.elisa", "--output", str(output),
                ])
            self.assertEqual(status, 17)
            self.assertEqual(output.read_text(), "stale")
            self.assertFalse((log_dir / "linker.json").exists())
            self.assertFalse((log_dir / "ran.json").exists())


if __name__ == "__main__":
    unittest.main()
