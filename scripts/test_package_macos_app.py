#!/usr/bin/env python3
"""Focused checks for manifest-driven macOS bundle staging."""

from __future__ import annotations

import json
import os
import plistlib
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

import package_macos_app as packager


def touch(path: Path, content: bytes = b"x") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


class PackageMacosAppTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        root = Path(self.tempdir.name)
        self.project = root / "Game with spaces"
        self.output = root / "Out dir" / "Game.app"
        touch(self.project / "build" / "game", b"#!/bin/sh\nexit 0\n")
        os.chmod(self.project / "build" / "game", 0o755)
        touch(self.project / "build" / "cooked" / "player.pkg")
        touch(self.project / "assets" / "audio" / "step.wav")
        touch(self.project / "assets" / "textures" / "trim.png")
        touch(self.project / "assets" / "source" / "rig.blend")
        touch(self.project / "assets" / ".git" / "HEAD")
        touch(self.project / "assets" / ".DS_Store")
        touch(self.project / "shaders" / "metal" / "basic.cso")
        touch(self.project / "shaders" / "metal" / "basic.wishadermeta")

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def write_manifest(self, extra: dict[str, object]) -> dict[str, object]:
        manifest: dict[str, object] = {
            "name": "Game", "main": "src/main.elisa", "output": "build/game",
            "application": {"title": "Game"},
        }
        manifest.update(extra)
        (self.project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
        return manifest

    def package(self, manifest: dict[str, object]) -> Path:
        return packager.package_app(self.project, self.project / "build" / "game",
            self.output, "Game", "org.elisa.game", "1.2.3", None,
            packager.manifest_resources(manifest, self.project),
            packager.manifest_window(manifest, self.project))

    def staged(self, app: Path) -> set[str]:
        resources = app / "Contents" / "Resources"
        return {str(path.relative_to(resources)) for path in resources.rglob("*") if path.is_file()}

    def test_declared_resources_stage_only_runtime_files(self) -> None:
        app = self.package(self.write_manifest({"package": {"resources": [
            "assets/audio", "assets/textures/trim.png", "assets/audio"]}}))
        self.assertEqual(self.staged(app), {
            "Game.bin", "assets/audio/step.wav", "assets/textures/trim.png",
            "build/cooked/player.pkg", "shaders/metal/basic.cso",
            "shaders/elisa.shader-manifest.json"})
        launcher = app / "Contents" / "MacOS" / "Game"
        self.assertTrue(launcher.stat().st_mode & stat.S_IXUSR)
        with (app / "Contents" / "Info.plist").open("rb") as stream:
            info = plistlib.load(stream)
        self.assertEqual(info["CFBundleExecutable"], "Game")
        self.assertEqual(info["CFBundleShortVersionString"], "1.2.3")

    def test_undeclared_resources_stage_assets_without_litter(self) -> None:
        app = self.package(self.write_manifest({}))
        self.assertEqual(self.staged(app), {
            "Game.bin", "assets/audio/step.wav", "assets/textures/trim.png",
            "assets/source/rig.blend", "build/cooked/player.pkg", "shaders/metal/basic.cso",
            "shaders/elisa.shader-manifest.json"})

    def test_launcher_runs_from_relocated_resources(self) -> None:
        app = self.package(self.write_manifest({"package": {"resources": ["assets/audio"]},
            "application": {"title": "Game", "width": 1100, "height": 820}}))
        binary = app / "Contents" / "Resources" / "Game.bin"
        binary.write_text("#!/bin/sh\npwd\ntest -f assets/audio/step.wav && echo found\n"
            "echo \"$ELISA_ENGINE_SHADER_PATH\"\n"
            "echo \"$ELISA_ENGINE_SHADER_MANIFEST\"\n"
            "echo \"$ELISA_PROJECT_TITLE $ELISA_PROJECT_WIDTH $ELISA_PROJECT_HEIGHT\"\n",
            encoding="utf-8")
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=True, cwd=self.tempdir.name)
        resources = (app / "Contents" / "Resources").resolve()
        self.assertEqual(result.stdout.splitlines(),
            [str(resources), "found", str(resources / "shaders"),
             str(resources / "shaders" / "elisa.shader-manifest.json"), "Game 1100 820"])
        override = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=True, cwd=self.tempdir.name,
            env={**os.environ, "ELISA_PROJECT_WIDTH": "640"})
        self.assertEqual(override.stdout.splitlines()[-1], "Game 640 820")

    def test_shader_manifest_is_deterministic_and_content_sensitive(self) -> None:
        manifest = packager.shader_manifest(self.project / "shaders")
        self.assertEqual(manifest["schema"], 1)
        self.assertEqual(len(manifest["files"]), 1)
        first = manifest["fingerprint"]
        (self.project / "shaders" / "metal" / "basic.cso").write_bytes(b"changed")
        self.assertNotEqual(first, packager.shader_manifest(self.project / "shaders")["fingerprint"])

    def test_invalid_window_sizes_are_rejected(self) -> None:
        for size in (0, -5, 20000, "wide", True):
            manifest = self.write_manifest({"application": {"title": "Game", "width": size}})
            with self.assertRaises(packager.PackageError):
                packager.manifest_window(manifest, self.project)

    def test_invalid_resource_declarations_are_rejected(self) -> None:
        for resources in ([], ["../escape"], ["/abs"], ["assets/.git"], [""], [3]):
            with self.subTest(resources=resources):
                with self.assertRaises(packager.PackageError):
                    packager.manifest_resources({"package": {"resources": resources}}, self.project)
        with self.assertRaises(packager.PackageError):
            packager.manifest_resources({"package": []}, self.project)
        missing = self.write_manifest({"package": {"resources": ["assets/missing.png"]}})
        with self.assertRaises(packager.PackageError):
            self.package(missing)

    def test_linked_libraries_are_bundled_transitively(self) -> None:
        clang = shutil.which("clang")
        if clang is None or shutil.which("install_name_tool") is None:
            self.skipTest("clang and install_name_tool are required")
        prefix = Path(self.tempdir.name) / "opt prefix" / "lib"
        prefix.mkdir(parents=True)
        touch(prefix / "deep.c", b"int deep_value(void) { return 4; }\n")
        touch(prefix / "tiny.c", b"int deep_value(void);\nint tiny_value(void) { return deep_value() + 3; }\n")
        touch(self.project / "main.c", b"#include <stdio.h>\nint tiny_value(void);\n"
            b"int main(void) { printf(\"%d\\n\", tiny_value()); return 0; }\n")
        deep = prefix / "libdeep.1.dylib"
        tiny = prefix / "libtiny.1.dylib"
        subprocess.run([clang, "-dynamiclib", "-install_name", str(deep), "-o", str(deep),
            str(prefix / "deep.c")], check=True)
        subprocess.run([clang, "-dynamiclib", "-install_name", str(tiny), "-o", str(tiny),
            str(prefix / "tiny.c"), str(deep)], check=True)
        subprocess.run([clang, "-o", str(self.project / "build" / "game"),
            str(self.project / "main.c"), str(tiny)], check=True)
        app = self.package(self.write_manifest({"package": {"resources": ["assets/audio"]}}))
        frameworks = app / "Contents" / "Frameworks"
        self.assertEqual({path.name for path in frameworks.iterdir()},
            {"libdeep.1.dylib", "libtiny.1.dylib"})
        binary = app / "Contents" / "Resources" / "Game.bin"
        self.assertIn("@rpath/libtiny.1.dylib", packager.linked_libraries(binary))
        self.assertIn("@rpath/libdeep.1.dylib", packager.linked_libraries(frameworks / "libtiny.1.dylib"))
        shutil.rmtree(prefix)
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=True, cwd=self.tempdir.name)
        self.assertEqual(result.stdout.strip(), "7")

    def test_symlinked_resource_is_rejected(self) -> None:
        os.symlink(self.project / "assets" / "source", self.project / "assets" / "link")
        with self.assertRaises(packager.PackageError):
            self.package(self.write_manifest({"package": {"resources": ["assets/link"]}}))


if __name__ == "__main__":
    unittest.main()
