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
        "program += 'settings = {key: os.environ.get(key) for key in (\"ELISA_PROJECT_TITLE\", \"ELISA_PROJECT_WIDTH\", \"ELISA_PROJECT_HEIGHT\", \"ELISA_PROJECT_HIDDEN\", \"ELISA_PROJECT_ROOT\")}\\n'\n"
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

    def test_declared_asset_cooks_resolve_inside_project(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa asset cook ") as temporary_directory:
            project = Path(temporary_directory) / "Project with spaces"
            source = project / "assets" / "walk.fbx"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"fbx")
            config = {"asset_cooks": [{
                "source": "assets/walk.fbx",
                "asset_path": "assets/walk.fbx",
                "output": "build/cooked/walk.pkg",
                "max_triangles": 120000,
            }]}
            runner = __import__("elisa_build_run")
            with mock.patch.object(runner, "run_command", return_value=0) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[0], sys.executable)
            self.assertEqual(command[1], str(SCRIPT.parent / "cook_fbx_asset.py"))
            self.assertEqual(command[2], str(source.resolve()))
            self.assertEqual(command[command.index("--output") + 1], str((project / "build/cooked/walk.pkg").resolve()))
            self.assertEqual(command[command.index("--max-triangles") + 1], "120000")
            self.assertEqual(run.call_args.kwargs["cwd"], project.resolve())
            config["asset_cooks"][0]["output"] = "../outside.pkg"
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), config)
            config["asset_cooks"][0]["output"] = "build/cooked/walk.pkg"
            config["asset_cooks"][0]["max_triangles"] = True
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), config)

    def test_declared_gltf_cook_uses_runtime_geometry_cooker(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa glTF asset cook ") as temporary_directory:
            project = Path(temporary_directory) / "Maze project"
            source = project / "assets" / "tile.gltf"
            source.parent.mkdir(parents=True)
            source.write_text("{}", encoding="utf-8")
            declaration = {
                "importer": "gltf",
                "source": "assets/tile.gltf",
                "asset_path": "assets/tile.gltf",
                "output": "assets/tile.pkg",
            }
            runner = __import__("elisa_build_run")
            with mock.patch.object(runner, "run_command", return_value=0) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [declaration]}), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[1], str(SCRIPT.parent / "cook_gltf_asset.py"))
            self.assertEqual(command[2], str(source.resolve()))
            self.assertEqual(command[command.index("--output") + 1],
                str((project / "assets/tile.pkg").resolve()))
            declaration["source"] = "assets/tile.glb"
            (project / "assets" / "tile.glb").write_bytes(b"glb")
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), {"asset_cooks": [declaration]})

    def test_declared_gltf_textures_become_bundle_sections(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa glTF textures ") as temporary_directory:
            project = Path(temporary_directory) / "Maze project"
            (project / "assets").mkdir(parents=True)
            (project / "assets" / "tile.gltf").write_text("{}", encoding="utf-8")
            (project / "assets" / "wall.png").write_bytes(b"png")
            (project / "assets" / "floor.jpg").write_bytes(b"jpg")
            declaration = {
                "importer": "gltf",
                "source": "assets/tile.gltf",
                "asset_path": "assets/tile.gltf",
                "output": "assets/tile.elpk",
                "textures": {"wall": "assets/wall.png", "floor": "assets/floor.jpg"},
            }
            runner = __import__("elisa_build_run")
            with mock.patch.object(runner, "run_command", return_value=0) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [declaration]}), 0)
            command = run.call_args.args[0]
            textures = [command[index + 1] for index, value in enumerate(command) if value == "--texture"]
            self.assertEqual(textures, [
                f"floor={(project / 'assets/floor.jpg').resolve()}",
                f"wall={(project / 'assets/wall.png').resolve()}",
            ])
            rejected = [
                {"textures": {"Wall": "assets/wall.png"}},
                {"textures": {"mesh": "assets/wall.png"}},
                {"textures": {"wall": "../wall.png"}},
                {"textures": {"wall": "assets/missing.png"}},
                {"textures": ["assets/wall.png"]},
                {"output": "assets/tile.pkg"},
            ]
            for change in rejected:
                with self.subTest(change=change), self.assertRaises(runner.BuildConfigurationError):
                    runner.cook_declared_assets(project.resolve(), {"asset_cooks": [{**declaration, **change}]})

    def test_bundle_dependencies_cook_first_with_relative_names(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa bundle dependencies ") as temporary_directory:
            project = Path(temporary_directory) / "Maze project"
            (project / "assets").mkdir(parents=True)
            (project / "assets" / "tile.gltf").write_text("{}", encoding="utf-8")
            (project / "assets" / "wall.png").write_bytes(b"png")
            tile = {
                "importer": "gltf",
                "source": "assets/tile.gltf",
                "asset_path": "assets/tile.gltf",
                "output": "assets/tile.elpk",
                "dependencies": ["assets/textures/wall.elpk"],
            }
            images = {
                "importer": "images",
                "output": "assets/textures/wall.elpk",
                "textures": {"wallalbedo": "assets/wall.png"},
            }
            runner = __import__("elisa_build_run")
            with mock.patch.object(runner, "run_command", return_value=0) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [tile, images]}), 0)
            first, second = (call.args[0] for call in run.call_args_list)
            self.assertEqual(first[1:], [
                str(SCRIPT.parent / "cook_image_bundle.py"),
                "--output", str((project / "assets/textures/wall.elpk").resolve()),
                "--texture", f"wallalbedo={(project / 'assets/wall.png').resolve()}",
            ])
            self.assertEqual(second[1], str(SCRIPT.parent / "cook_gltf_asset.py"))
            self.assertEqual(second[second.index("--dependency") + 1], "textures/wall.elpk")
            rejected = [
                ("no cook writes the dependency", [{**tile, "dependencies": ["assets/textures/other.elpk"]}, images]),
                ("self dependency", [{**tile, "dependencies": ["assets/tile.elpk"]}, images]),
                ("dependency outside the bundle's directory",
                    [{**tile, "output": "assets/tiles/tile.elpk"}, images]),
                ("repeated dependency",
                    [{**tile, "dependencies": ["assets/textures/wall.elpk"] * 2}, images]),
                ("dependency cycle", [{**tile, "dependencies": ["assets/wall.elpk"]},
                    {**images, "output": "assets/wall.elpk", "dependencies": ["assets/tile.elpk"]}]),
                ("dependency from a loose package", [{**tile, "output": "assets/tile.pkg"}, images]),
                ("dependencies not an array", [{**tile, "dependencies": "assets/textures/wall.elpk"}, images]),
                ("images cook with a source", [tile, {**images, "source": "assets/wall.png"}]),
                ("images cook without textures", [tile, {**images, "textures": {}}]),
                ("two cooks write one bundle", [tile, images, images]),
            ]
            for label, cooks in rejected:
                with self.subTest(label), self.assertRaises(runner.BuildConfigurationError), \
                        mock.patch.object(runner, "run_command", return_value=0) as run:
                    runner.cook_declared_assets(project.resolve(), {"asset_cooks": cooks})
                self.assertFalse(run.called, label)

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
