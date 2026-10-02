"""Declared asset cook checks for the elisa_build_run CLI.

Collected by test_elisa_build_run.py, which imports AssetCookTests.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from elisa_build_run_test_support import (
    SCRIPT, assert_staged_output, fake_cook_command, mocked_asset_cooker,
)


class AssetCookTests(unittest.TestCase):
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
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[0], sys.executable)
            self.assertEqual(command[1], str(SCRIPT.parent / "cook_fbx_asset.py"))
            self.assertEqual(command[2], str(source.resolve()))
            assert_staged_output(self, command, "--output", project / "build/cooked/walk.pkg")
            self.assertEqual(command[command.index("--max-triangles") + 1], "120000")
            self.assertEqual(run.call_args.kwargs["cwd"], project.resolve())
            config["asset_cooks"][0]["output"] = "../outside.pkg"
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), config)
            config["asset_cooks"][0]["output"] = "build/cooked/walk.pkg"
            config["asset_cooks"][0]["max_triangles"] = True
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), config)
            config["asset_cooks"][0]["max_triangles"] = 120000
            config["asset_cooks"][0]["ignore_material_textures"] = True
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
            self.assertIn("--ignore-material-textures", run.call_args.args[0])
            config["asset_cooks"][0]["ignore_material_textures"] = False
            config["asset_cooks"][0]["all_meshes"] = True
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
            self.assertIn("--all-meshes", run.call_args.args[0])
            config["asset_cooks"][0]["all_meshes"] = "true"
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), config)
            config["asset_cooks"][0]["all_meshes"] = False
            config["asset_cooks"][0]["ignore_material_textures"] = "true"
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), config)

    def test_asset_cook_cache_hits_invalidates_repairs_and_forces(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa cook cache ") as temporary_directory:
            project = Path(temporary_directory) / "Project"
            source = project / "assets" / "walk.fbx"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"source version 1")
            output = project / "build/cooked/walk.pkg"
            config = {"asset_cooks": [{
                "source": "assets/walk.fbx", "asset_path": "assets/walk.fbx",
                "output": "build/cooked/walk.pkg",
            }]}
            runner = __import__("elisa_build_run")
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 1)

                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 1, "unchanged inputs should use the verified cook cache")

                source.write_bytes(b"source version 2")
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 2, "source content changes must invalidate the cache")

                output.unlink()
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 3, "missing outputs must be recooked")

                output.write_bytes(b"modified output")
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 4, "modified outputs must be recooked")

                self.assertEqual(runner.cook_declared_assets(project.resolve(), config, force=True), 0)
                self.assertEqual(len(run.call_args_list), 5, "forced cooks must bypass valid cache entries")
                self.assertNotEqual(output.read_bytes(), b"modified output")

            record = json.loads((project / "build/.elisa-asset-cook-cache.json").read_text())
            self.assertEqual(record["format"], 1)
            cached_outputs = record["entries"]["build/cooked/walk.pkg"]["outputs"]
            self.assertEqual([item["path"] for item in cached_outputs], ["build/cooked/walk.pkg"])

    def test_asset_cook_cache_recooks_only_changed_source(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa selective asset cooks ") as temporary_directory:
            project = Path(temporary_directory) / "Project"
            assets = project / "assets"
            assets.mkdir(parents=True)
            first_source = assets / "first.fbx"
            second_source = assets / "second.fbx"
            first_source.write_bytes(b"first version 1")
            second_source.write_bytes(b"second version 1")
            config = {"asset_cooks": [
                {"source": "assets/first.fbx", "asset_path": "assets/first.fbx",
                    "output": "build/first.pkg"},
                {"source": "assets/second.fbx", "asset_path": "assets/second.fbx",
                    "output": "build/second.pkg"},
            ]}
            runner = __import__("elisa_build_run")
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 2)
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 2)

                first_source.write_bytes(b"first version 2")
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 3,
                    "editing one source should leave the other cook cached")
                changed_command = run.call_args_list[-1].args[0]
                self.assertEqual(changed_command[2], str(first_source.resolve()))

    def test_gltf_external_resources_and_glb_extracted_texture_invalidate_cache(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa cook input dependencies ") as temporary_directory:
            project = Path(temporary_directory) / "Project"
            assets = project / "assets"
            assets.mkdir(parents=True)
            gltf = assets / "tile.gltf"
            texture = assets / "wall.png"
            gltf.write_text(json.dumps({"images": [{"uri": "wall.png"}]}), encoding="utf-8")
            texture.write_bytes(b"texture 1")
            glb = assets / "cyborg.glb"
            glb.write_bytes(b"glb")
            runner = __import__("elisa_build_run")
            config = {"asset_cooks": [{
                "importer": "gltf", "source": "assets/tile.gltf",
                "asset_path": "assets/tile.gltf", "output": "build/tile.pkg",
            }]}
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 1)
                texture.write_bytes(b"texture 2")
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 2,
                    "a glTF external image edit must invalidate its package")

                glb_config = {"asset_cooks": [{
                    "importer": "glb", "source": "assets/cyborg.glb",
                    "asset_path": "assets/cyborg.glb", "output": "build/cyborg.pkg",
                    "texture_output": "build/cyborg.png",
                }]}
                self.assertEqual(runner.cook_declared_assets(project.resolve(), glb_config), 0)
                self.assertEqual(len(run.call_args_list), 3)
                self.assertEqual(runner.cook_declared_assets(project.resolve(), glb_config), 0)
                self.assertEqual(len(run.call_args_list), 3)

                texture_output = project / "build/cyborg.png"
                texture_output.unlink()
                self.assertEqual(runner.cook_declared_assets(project.resolve(), glb_config), 0)
                self.assertEqual(len(run.call_args_list), 4,
                    "a missing extracted texture must recook the GLB package")

    def test_native_cooker_source_edits_invalidate_asset_cache(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa native cook inputs ") as temporary_directory:
            project = Path(temporary_directory) / "Project"
            source = project / "assets/wall.fbx"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"fbx")
            config = {"asset_cooks": [{
                "source": "assets/wall.fbx", "asset_path": "assets/wall.fbx",
                "output": "build/wall.pkg",
            }]}
            runner = __import__("elisa_build_run")
            native_dependency = runner.asset_cooks.ENGINE_ROOT / "native/fbx_asset_cooker.cpp"
            native_revision = ["cooker source revision 1"]
            real_hash = runner.asset_cooks.sha256_file

            def hash_with_native_revision(path: Path) -> str:
                if path == native_dependency:
                    return native_revision[0]
                return real_hash(path)

            with mock.patch.object(runner.asset_cooks, "_external_tool_identity",
                    return_value={"toolchain": "fake"}), \
                    mock.patch.object(runner.asset_cooks, "local_source_closure",
                        return_value=[native_dependency]), \
                    mock.patch.object(runner.asset_cooks, "sha256_file",
                        side_effect=hash_with_native_revision), \
                    mock.patch.object(runner, "run_command", side_effect=fake_cook_command) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 1)
                native_revision[0] = "cooker source revision 2"
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                self.assertEqual(len(run.call_args_list), 2,
                    "native cooker source edits must invalidate cooked assets")

    def test_failed_asset_cook_keeps_last_good_outputs(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa failed cook ") as temporary_directory:
            project = Path(temporary_directory) / "Project"
            source = project / "assets/wall.fbx"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"source")
            output = project / "build/wall.pkg"
            output.parent.mkdir(parents=True)
            output.write_bytes(b"last good package")
            config = {"asset_cooks": [{
                "source": "assets/wall.fbx", "asset_path": "assets/wall.fbx",
                "output": "build/wall.pkg",
            }]}
            runner = __import__("elisa_build_run")
            with mock.patch.object(runner.asset_cooks, "_external_tool_identity",
                    return_value={"toolchain": "fake"}), \
                    mock.patch.object(runner, "run_command", side_effect=fake_cook_command) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 0)
                # Start with a real cache and then fail after changing the source.
                output.write_bytes(b"last good package")
                source.write_bytes(b"changed source")

                def failing_cook(command: list[str], *, cwd: Path) -> int:
                    fake_cook_command(command, cwd=cwd)
                    return 1

                run.side_effect = failing_cook
                self.assertEqual(runner.cook_declared_assets(project.resolve(), config), 1)
            self.assertEqual(output.read_bytes(), b"last good package")
            staged_files = list(output.parent.glob(f".{output.stem}.elisa-cook-*{output.suffix}"))
            self.assertEqual(staged_files, [])

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
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [declaration]}), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[1], str(SCRIPT.parent / "cook_gltf_asset.py"))
            self.assertEqual(command[2], str(source.resolve()))
            assert_staged_output(self, command, "--output", project / "assets/tile.pkg")
            declaration["source"] = "assets/tile.glb"
            (project / "assets" / "tile.glb").write_bytes(b"glb")
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), {"asset_cooks": [declaration]})

    def test_declared_glb_cook_forwards_rig_clips_and_texture_output(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa GLB asset cook ") as temporary_directory:
            project = Path(temporary_directory) / "Maze project"
            source = project / "assets" / "cyborg.glb"
            animation = project / "assets" / "locomotion.fbx"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"glb")
            animation.write_bytes(b"fbx")
            declaration = {
                "importer": "glb",
                "source": "assets/cyborg.glb",
                "asset_path": "assets/cyborg.glb",
                "output": "build/cooked/cyborg.pkg",
                "animation_source": "assets/locomotion.fbx",
                "texture_output": "build/cooked/cyborg-basecolor.png",
            }
            runner = __import__("elisa_build_run")
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [declaration]}), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[1], str(SCRIPT.parent / "cook_glb_asset.py"))
            self.assertEqual(command[2], str(source.resolve()))
            self.assertEqual(command[command.index("--animation-source") + 1], str(animation.resolve()))
            declaration["texture_max_size"] = 2048
            with mocked_asset_cooker(runner) as bounded:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [declaration]}), 0)
            bounded_command = bounded.call_args.args[0]
            self.assertEqual(bounded_command[bounded_command.index("--texture-max-size") + 1], "2048")
            for broken in ({"texture_max_size": 0}, {"texture_max_size": "big"}):
                with self.assertRaises(runner.BuildConfigurationError):
                    runner.cook_declared_assets(project.resolve(), {"asset_cooks": [{**declaration, **broken}]})
            unbounded = {key: value for key, value in declaration.items() if key != "texture_output"}
            with self.assertRaises(runner.BuildConfigurationError):
                runner.cook_declared_assets(project.resolve(), {"asset_cooks": [unbounded]})
            del declaration["texture_max_size"]
            assert_staged_output(self, command, "--texture-output",
                project / "build/cooked/cyborg-basecolor.png")
            bounded = {**declaration, "max_triangles": 1000}
            with mocked_asset_cooker(runner) as bounded_run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [bounded]}), 0)
            bounded_command = bounded_run.call_args.args[0]
            self.assertEqual(bounded_command[bounded_command.index("--max-triangles") + 1], "1000")
            for changes in (
                {"source": "assets/cyborg.gltf"},
                {"animation_source": "../outside.fbx"},
                {"animation_source": "assets/cyborg.glb"},
                {"texture_output": "../outside.png"},
                {"texture_output": "assets/cyborg.glb"},
                {"max_triangles": 0},
                {"max_triangles": 1000001},
                {"max_triangles": True},
            ):
                with self.subTest(changes=changes), self.assertRaises(runner.BuildConfigurationError):
                    runner.cook_declared_assets(project.resolve(), {
                        "asset_cooks": [{**declaration, **changes}]})

    def test_declared_image_cook_bounds_a_texture(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa image asset cook ") as temporary_directory:
            project = Path(temporary_directory) / "Maze project"
            source = project / "assets" / "crate.png"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"png")
            declaration = {"importer": "image", "source": "assets/crate.png",
                "output": "build/cooked/textures/crate.png", "max_size": 2048}
            runner = __import__("elisa_build_run")
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [declaration]}), 0)
            command = run.call_args.args[0]
            self.assertEqual(command[1], str(SCRIPT.parent / "cook_image_asset.py"))
            self.assertEqual(command[2], str(source.resolve()))
            assert_staged_output(self, command, "--output",
                project / "build/cooked/textures/crate.png")
            self.assertEqual(command[command.index("--max-size") + 1], "2048")
            for broken in ({"max_size": 0}, {"max_size": True}, {"max_size": 9000},
                    {"output": "build/cooked/crate.jpg"}, {"asset_path": "assets/crate.png"},
                    {"textures": {"albedo": "assets/crate.png"}}, {"output": "assets/crate.png"}):
                with self.assertRaises(runner.BuildConfigurationError):
                    runner.cook_declared_assets(project.resolve(), {"asset_cooks": [{**declaration, **broken}]})
            del declaration["max_size"]
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
            with mocked_asset_cooker(runner) as run:
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
            with mocked_asset_cooker(runner) as run:
                self.assertEqual(runner.cook_declared_assets(project.resolve(), {
                    "asset_cooks": [tile, images]}), 0)
            first, second = (call.args[0] for call in run.call_args_list)
            self.assertEqual(first[1], str(SCRIPT.parent / "cook_image_bundle.py"))
            assert_staged_output(self, first, "--output", project / "assets/textures/wall.elpk")
            self.assertEqual(first[first.index("--texture") + 1],
                f"wallalbedo={(project / 'assets/wall.png').resolve()}")
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
