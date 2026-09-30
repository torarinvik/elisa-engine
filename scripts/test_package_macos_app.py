#!/usr/bin/env python3
"""Focused checks for manifest-driven macOS bundle staging."""

from __future__ import annotations

import hashlib
import json
import os
import plistlib
import shutil
import stat
import subprocess
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import package_macos_app as packager

ROOT = Path(__file__).resolve().parents[1]


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
        touch(self.project / "shaders" / packager.SHADER_GENERATED_INVENTORY_NAME)

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
            packager.manifest_window(manifest, self.project),
            notice_paths=packager.manifest_notices(manifest, self.project))

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

    def test_build_provenance_is_embedded_and_local_paths_are_scrubbed(self) -> None:
        executable = self.project / "build/game"
        executable.write_bytes(b"built payload")
        source_provenance = {
            "build_identity": "0011223344556677",
            "project_root": str(self.project),
            "main_source": str(self.project / "src/main.elisa"),
            "repositories": {"game": {"root": str(self.project), "commit": "abc"}},
            "tools": {"elisa_compiler": {"resolved": "/opt/private/bin/elisac"}},
            "native_link_artifacts": [{"path": "/private/build/libWickedEngine.a", "sha256": "def"}],
            "options": {
                "wicked_build": "/private/build/wicked",
                "compiler_request": "/opt/private/bin/elisac",
            },
            "binary": {"path": str(executable), "sha256": "source-hash"},
        }
        sidecar = executable.with_name(executable.name + ".provenance.json")
        sidecar.write_text(json.dumps(source_provenance), encoding="utf-8")
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        resource_root = app / "Contents/Resources"
        record_path = resource_root / "build-provenance.json"
        self.assertTrue(record_path.is_file())
        record = json.loads(record_path.read_text(encoding="utf-8"))
        self.assertEqual(record["build_identity"], "0011223344556677")
        self.assertEqual(record["repositories"]["game"]["commit"], "abc")
        self.assertEqual(record["repositories"]["game"]["root"], "<game>")
        self.assertEqual(record["tools"]["elisa_compiler"]["resolved"], "elisac")
        self.assertEqual(record["native_link_artifacts"][0]["path"], "libWickedEngine.a")
        self.assertEqual(record["binary"]["path"], "build executable")
        self.assertEqual(record["options"]["compiler_request"], "elisac")
        self.assertEqual(record["packaged_binary"]["sha256"],
            hashlib.sha256((resource_root / "Game.bin").read_bytes()).hexdigest())
        self.assertNotIn(str(self.project), record_path.read_text(encoding="utf-8"))

    def test_empty_resource_declaration_stages_no_project_files(self) -> None:
        shutil.rmtree(self.project / "assets")
        shutil.rmtree(self.project / "build" / "cooked")
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        self.assertEqual(self.staged(app), {
            "Game.bin", "shaders/metal/basic.cso", "shaders/elisa.shader-manifest.json"})

    def test_undeclared_resources_stage_assets_without_litter(self) -> None:
        app = self.package(self.write_manifest({}))
        self.assertEqual(self.staged(app), {
            "Game.bin", "assets/audio/step.wav", "assets/textures/trim.png",
            "assets/source/rig.blend", "build/cooked/player.pkg", "shaders/metal/basic.cso",
            "shaders/elisa.shader-manifest.json"})

    def test_launcher_runs_from_relocated_resources(self) -> None:
        executable = self.project / "build" / "game"
        sidecar = executable.with_name(executable.name + ".provenance.json")
        sidecar.write_text(json.dumps({"build_identity": "0011223344556677"}), encoding="utf-8")
        app = self.package(self.write_manifest({"package": {"resources": ["assets/audio"]},
            "application": {"title": "Game", "width": 1100, "height": 820}}))
        binary = app / "Contents" / "Resources" / "Game.bin"
        binary.write_text("#!/bin/sh\npwd\ntest -f assets/audio/step.wav && echo found\n"
            "echo \"$ELISA_ENGINE_SHADER_PATH\"\n"
            "echo \"$ELISA_ENGINE_SHADER_MANIFEST\"\n"
            "echo \"$ELISA_PROJECT_TITLE $ELISA_PROJECT_WIDTH $ELISA_PROJECT_HEIGHT\"\n",
            encoding="utf-8")
        os.chmod(binary, 0o755)
        home = Path(self.tempdir.name) / "home"
        environment = {**os.environ, "HOME": str(home)}
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=True, cwd=self.tempdir.name,
            env=environment)
        resources = (app / "Contents" / "Resources").resolve()
        self.assertEqual(result.stdout, "")
        log = home / "Library/Logs/Elisa/org.elisa.game/latest.log"
        log_lines = log.read_text(encoding="utf-8").splitlines()
        self.assertTrue(all(expected in log_lines for expected in (
            str(resources), "found", str(resources / "shaders"),
            str(resources / "shaders" / "elisa.shader-manifest.json"), "Game 1100 820")))
        self.assertIn("build_identity=0011223344556677", log_lines)
        self.assertIn("process_exit_status=0", log_lines)
        executable_hash = hashlib.sha256((self.project / "build" / "game").read_bytes()).hexdigest()
        self.assertIn(f"executable-sha256={executable_hash}", result.stderr)
        self.assertIn("build-identity=0011223344556677", result.stderr)
        self.assertIn(f"launcher_log={log}", result.stderr)
        self.assertIn("Elisa process exit status: 0", result.stderr)
        prior_log = log.read_text(encoding="utf-8")
        override = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=True, cwd=self.tempdir.name,
            env={**environment, "ELISA_PROJECT_WIDTH": "640"})
        current_log = log.read_text(encoding="utf-8")
        previous_log = log.with_name("previous.log").read_text(encoding="utf-8")
        self.assertIn("Game 640 820", current_log.splitlines())
        self.assertIn("Game 1100 820", previous_log.splitlines())
        self.assertEqual(previous_log, prior_log)
        self.assertEqual(override.stdout, "")

    def test_launcher_preserves_nonzero_exit_and_records_failure_output(self) -> None:
        binary = self.project / "build" / "game"
        binary.write_text("#!/bin/sh\nprintf 'runtime failure\\n' >&2\nexit 37\n", encoding="utf-8")
        os.chmod(binary, 0o755)
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        env = {**os.environ, "HOME": self.tempdir.name}
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=False, cwd=self.tempdir.name, env=env)
        log = Path(self.tempdir.name) / "Library/Logs/Elisa/org.elisa.game/latest.log"
        self.assertEqual(result.returncode, 37)
        self.assertIn("runtime failure", log.read_text(encoding="utf-8"))
        self.assertIn("process_exit_status=37", log.read_text(encoding="utf-8"))
        self.assertIn("Elisa process exit status: 37", result.stderr)

    def test_launcher_still_runs_when_log_directory_is_unavailable(self) -> None:
        binary = self.project / "build" / "game"
        binary.write_text("#!/bin/sh\nexit 37\n", encoding="utf-8")
        os.chmod(binary, 0o755)
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        home_file = Path(self.tempdir.name) / "home-file"
        home_file.write_text("not a directory", encoding="utf-8")
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=False, cwd=self.tempdir.name,
            env={**os.environ, "HOME": str(home_file)})
        self.assertEqual(result.returncode, 37)
        self.assertIn("Elisa process exit status: 37", result.stderr)

    def test_launcher_records_signal_style_termination_status(self) -> None:
        binary = self.project / "build" / "game"
        binary.write_text("#!/bin/sh\nkill -TERM $$\n", encoding="utf-8")
        os.chmod(binary, 0o755)
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        env = {**os.environ, "HOME": self.tempdir.name}
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=False, cwd=self.tempdir.name, env=env)
        log = Path(self.tempdir.name) / "Library/Logs/Elisa/org.elisa.game/latest.log"
        self.assertEqual(result.returncode, 143)
        self.assertIn("process_exit_status=143", log.read_text(encoding="utf-8"))
        self.assertIn("Elisa process exit status: 143", result.stderr)

    def test_launcher_links_recent_matching_macos_crash_reports(self) -> None:
        binary = self.project / "build" / "game"
        binary.write_text("#!/bin/sh\nexit 139\n", encoding="utf-8")
        os.chmod(binary, 0o755)
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        diagnostic_reports = Path(self.tempdir.name) / "Library/Logs/DiagnosticReports"
        matching = diagnostic_reports / "Game-2026-09-30-120000.ips"
        stale = diagnostic_reports / "Game-old.crash"
        unrelated = diagnostic_reports / "OtherGame-2026-09-30-120000.ips"
        touch(matching)
        touch(stale)
        touch(unrelated)
        os.utime(stale, (0, 0))
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=False, cwd=self.tempdir.name,
            env={**os.environ, "HOME": self.tempdir.name})
        log = Path(self.tempdir.name) / "Library/Logs/Elisa/org.elisa.game/latest.log"
        content = log.read_text(encoding="utf-8")
        self.assertEqual(result.returncode, 139)
        self.assertIn("recent macOS crash report candidates", content)
        self.assertIn(str(matching), content)
        self.assertNotIn(str(stale), content)
        self.assertNotIn(str(unrelated), content)

    def test_unsafe_bundle_identifier_is_rejected(self) -> None:
        with self.assertRaises(packager.PackageError):
            packager.package_app(self.project, self.project / "build/game", self.output,
                "Game", "org.elisa;touch", "1.0")

    def test_launcher_configures_local_crash_reports(self) -> None:
        sidecar = self.project / "build" / "game.provenance.json"
        sidecar.write_text(json.dumps({"build_identity": "0011223344556677"}),
            encoding="utf-8")
        app = self.package(self.write_manifest({"application": {"title": "Game"}}))
        binary = app / "Contents" / "Resources" / "Game.bin"
        binary.write_text("#!/bin/sh\necho \"${ELISA_CRASH_DIR:-off}\"\necho \"$ELISA_BUILD_IDENTITY\"\n",
            encoding="utf-8")
        launcher = str(app / "Contents" / "MacOS" / "Game")
        home = Path(self.tempdir.name) / "home"
        result = subprocess.run([launcher], capture_output=True, text=True, check=True,
            env={**os.environ, "HOME": str(home)})
        log = home / "Library/Logs/Elisa/org.elisa.game/latest.log"
        content = log.read_text(encoding="utf-8")
        self.assertIn(str(home / "Library/Logs/Game"), content)
        self.assertTrue((home / "Library/Logs/Game").is_dir())
        self.assertIn("0011223344556677", content)
        self.assertIn("0011223344556677", result.stderr)
        chosen = Path(self.tempdir.name) / "chosen reports"
        explicit = subprocess.run([launcher], capture_output=True, text=True, check=True,
            env={**os.environ, "HOME": str(home), "ELISA_CRASH_DIR": str(chosen)})
        self.assertIn(str(chosen), log.read_text(encoding="utf-8"))
        blocker = Path(self.tempdir.name) / "blocker"
        blocker.write_text("file", encoding="utf-8")
        unusable = subprocess.run([launcher], capture_output=True, text=True, check=True,
            env={**os.environ, "HOME": str(home), "ELISA_CRASH_DIR": str(blocker / "sub")})
        self.assertIn("off", log.read_text(encoding="utf-8"))

    def test_external_shader_library_and_asset_local_cooks(self) -> None:
        shutil.rmtree(self.project / "build/cooked")
        external = Path(self.tempdir.name) / "prepared shaders"
        touch(external / "metal/selected.cso", b"selected")
        app = packager.package_app(self.project, self.project / "build/game",
            self.output, "Game", "org.elisa.game", "1.0", shader_root=external)
        resources = app / "Contents/Resources"
        self.assertFalse((resources / "build/cooked").exists())
        self.assertEqual((resources / "shaders/metal/selected.cso").read_bytes(), b"selected")
        self.assertFalse((resources / "shaders/metal/basic.cso").exists())

    def test_output_cannot_erase_external_shader_input(self) -> None:
        shaders = self.output / "prepared"
        touch(shaders / "metal/keep.cso", b"keep")
        with self.assertRaises(packager.PackageError):
            packager.package_app(self.project, self.project / "build/game", self.output,
                "Game", "org.elisa.game", "1.0", shader_root=shaders)
        self.assertEqual((shaders / "metal/keep.cso").read_bytes(), b"keep")

    def test_notices_are_staged_with_original_bytes(self) -> None:
        touch(self.project / "third_party/SDL/LICENSE.txt", b"SDL notice\n")
        touch(self.project / "third_party/Jolt/LICENSE.txt", b"Jolt notice\n")
        app = self.package(self.write_manifest({"package": {"notices": [
            "third_party/SDL/LICENSE.txt", "third_party/Jolt/LICENSE.txt"]}}))
        root = app / "Contents/Resources/Notices/third_party"
        self.assertEqual((root / "SDL/LICENSE.txt").read_bytes(), b"SDL notice\n")
        self.assertEqual((root / "Jolt/LICENSE.txt").read_bytes(), b"Jolt notice\n")

    def test_missing_or_escaping_notices_are_rejected(self) -> None:
        for entry in ("missing.txt", "../outside.txt", "/absolute.txt"):
            with self.subTest(entry=entry), self.assertRaises(packager.PackageError):
                packager.manifest_notices({"package": {"notices": [entry]}}, self.project)

    def test_duplicate_and_empty_notices_are_rejected(self) -> None:
        touch(self.project / "notice.txt", b"notice")
        touch(self.project / "empty.txt", b"")
        for entries in (["notice.txt", "notice.txt"], ["empty.txt"]):
            with self.subTest(entries=entries), self.assertRaises(packager.PackageError):
                packager.manifest_notices({"package": {"notices": entries}}, self.project)

    def test_failed_rebuild_keeps_previous_app(self) -> None:
        manifest = self.write_manifest({})
        app = self.package(manifest)
        original = (app / "Contents/Info.plist").read_bytes()
        (self.project / "shaders/metal/basic.cso").unlink()
        with self.assertRaises(packager.PackageError):
            self.package(manifest)
        self.assertEqual((app / "Contents/Info.plist").read_bytes(), original)
        self.assertTrue((app / "Contents/Resources/shaders/metal/basic.cso").exists())

    def test_failed_publication_restores_previous_app(self) -> None:
        manifest = self.write_manifest({})
        app = self.package(manifest)
        marker = app / "previous-version"
        marker.write_bytes(b"previous")
        replace = os.replace
        def fail_stage(source, destination):
            if Path(source).parent.name.startswith(".elisa-app-stage-"):
                raise OSError("publication failure")
            return replace(source, destination)
        with mock.patch.object(packager.os, "replace", side_effect=fail_stage):
            with self.assertRaises(OSError):
                self.package(manifest)
        self.assertEqual(marker.read_bytes(), b"previous")
        self.assertEqual(list(app.parent.glob(".elisa-app-backup-*")), [])

    def test_bundle_cannot_be_created_inside_resource_source(self) -> None:
        with self.assertRaises(packager.PackageError):
            packager.package_app(self.project, self.project / "build/game",
                self.project / "assets/Recursive.app", "Game", "org.elisa.game", "1.0")
        self.assertFalse((self.project / "assets/Recursive.app").exists())

    def test_missing_cooked_directory_cannot_become_output_parent(self) -> None:
        shutil.rmtree(self.project / "build/cooked")
        with self.assertRaises(packager.PackageError):
            packager.package_app(self.project, self.project / "build/game",
                self.project / "build/cooked/Recursive.app", "Game", "org.elisa.game", "1.0")
        self.assertFalse((self.project / "build/cooked").exists())

    def test_shader_manifest_is_deterministic_and_content_sensitive(self) -> None:
        manifest = packager.shader_manifest(self.project / "shaders")
        self.assertEqual(manifest["schema"], 2)
        self.assertEqual(manifest["backends"], ["metal"])
        self.assertEqual(len(manifest["files"]), 1)
        first = manifest["fingerprint"]
        (self.project / "shaders" / "metal" / "basic.cso").write_bytes(b"changed")
        self.assertNotEqual(first, packager.shader_manifest(self.project / "shaders")["fingerprint"])

    def test_shader_manifest_records_every_compiled_backend(self) -> None:
        touch(self.project / "shaders" / "spirv" / "basic.spv", b"spirv")
        manifest = packager.shader_manifest(self.project / "shaders")
        self.assertEqual(manifest["backends"], ["metal", "spirv"])
        self.assertEqual(len(manifest["files"]), 2)

    def test_shader_manifest_rejects_binaries_outside_backend_directories(self) -> None:
        touch(self.project / "shaders" / "basic.cso", b"orphan")
        with self.assertRaises(packager.PackageError):
            packager.shader_manifest(self.project / "shaders")

    def test_native_shader_manifest_validation(self) -> None:
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("a C++ compiler is required")
        executable = Path(self.tempdir.name) / "shader-manifest-validation"
        command = [compiler, "-std=c++17", "-I", str(ROOT / "native")]
        for include_root in (Path("/opt/homebrew/include"), Path("/usr/local/include")):
            if include_root.is_dir():
                command.extend(["-I", str(include_root)])
        command.extend([str(ROOT / "test/shader_manifest_validation_native.cpp"), "-o", str(executable)])
        subprocess.run(command, check=True, capture_output=True, text=True)
        subprocess.run([str(executable)], check=True)

    def test_invalid_window_sizes_are_rejected(self) -> None:
        for size in (0, -5, 20000, "wide", True):
            manifest = self.write_manifest({"application": {"title": "Game", "width": size}})
            with self.assertRaises(packager.PackageError):
                packager.manifest_window(manifest, self.project)

    def test_invalid_resource_declarations_are_rejected(self) -> None:
        for resources in (["../escape"], ["/abs"], ["assets/.git"], [""], [3]):
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
        env = {**os.environ, "HOME": self.tempdir.name}
        result = subprocess.run([str(app / "Contents" / "MacOS" / "Game")],
            capture_output=True, text=True, check=True, cwd=self.tempdir.name, env=env)
        self.assertEqual(result.stdout, "")
        log = Path(self.tempdir.name) / "Library/Logs/Elisa/org.elisa.game/latest.log"
        self.assertIn("7", log.read_text(encoding="utf-8").splitlines())

    def test_symlinked_resource_is_rejected(self) -> None:
        os.symlink(self.project / "assets" / "source", self.project / "assets" / "link")
        with self.assertRaises(packager.PackageError):
            self.package(self.write_manifest({"package": {"resources": ["assets/link"]}}))


if __name__ == "__main__":
    unittest.main()
