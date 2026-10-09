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
from package_test_support import PackageAppTestSupport, touch

ROOT = Path(__file__).resolve().parents[1]


class PackageMacosAppTests(PackageAppTestSupport, unittest.TestCase):

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
            "project_manifest_sha256": "f" * 64,
            "main_source": str(self.project / "src/main.elisa"),
            "repositories": {"game": {"root": str(self.project), "commit": "abc"}},
            "tools": {"elisa_compiler": {"resolved": "/opt/private/bin/elisac"}},
            "native_link_artifacts": [{"path": "/private/build/libWickedEngine.a", "sha256": "def"}],
            "options": {
                "wicked_build": "/private/build/wicked",
                "compiler_request": "/opt/private/bin/elisac",
            },
            "binary": {"path": str(executable),
                "sha256": hashlib.sha256(executable.read_bytes()).hexdigest()},
        }
        sidecar = executable.with_name(executable.name + ".provenance.json")
        sidecar.write_text(json.dumps(source_provenance), encoding="utf-8")
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        resource_root = app / "Contents/Resources"
        record_path = resource_root / "build-provenance.json"
        self.assertTrue(record_path.is_file())
        record = json.loads(record_path.read_text(encoding="utf-8"))
        package_record_path = resource_root / "package-provenance.json"
        package_record = json.loads(package_record_path.read_text(encoding="utf-8"))
        self.assertEqual(record["build_identity"], "0011223344556677")
        self.assertEqual(record["project_manifest_sha256"], "f" * 64)
        self.assertEqual(record["repositories"]["game"]["commit"], "abc")
        self.assertEqual(record["repositories"]["game"]["root"], "<game>")
        self.assertEqual(record["tools"]["elisa_compiler"]["resolved"], "elisac")
        self.assertEqual(record["native_link_artifacts"][0]["path"], "libWickedEngine.a")
        self.assertEqual(record["binary"]["path"], "build executable")
        self.assertEqual(record["options"]["compiler_request"], "elisac")
        self.assertEqual(record["packaged_binary"]["sha256"],
            hashlib.sha256((resource_root / "Game.bin").read_bytes()).hexdigest())
        manifest_bytes = (self.project / "elisa.project.json").read_bytes()
        self.assertEqual(package_record["package_manifest_sha256"],
            hashlib.sha256(manifest_bytes).hexdigest())
        self.assertEqual(package_record["layout"]["resource_mode"], "allowlist")
        self.assertEqual(package_record["layout"]["resources"], [])
        shader_manifest = resource_root / "shaders" / packager.SHADER_MANIFEST_NAME
        self.assertEqual(package_record["layout"]["shader_manifest_sha256"],
            hashlib.sha256(shader_manifest.read_bytes()).hexdigest())
        self.assertFalse(package_record["layout"]["compiled_shaders_only"])
        self.assertEqual(package_record["payload_root"], "Contents")
        indexed_payload = {entry["path"]: entry for entry in package_record["payload_files"]}
        self.assertIn("Resources/Game.bin", indexed_payload)
        self.assertIn("MacOS/Game", indexed_payload)
        self.assertEqual(indexed_payload["Resources/Game.bin"]["sha256"],
            hashlib.sha256((resource_root / "Game.bin").read_bytes()).hexdigest())
        self.assertNotIn(str(self.project), record_path.read_text(encoding="utf-8"))
        self.assertNotIn(str(self.project), package_record_path.read_text(encoding="utf-8"))

    def test_stale_build_provenance_is_rejected_without_replacing_app(self) -> None:
        executable = self.project / "build/game"
        sidecar = executable.with_name(executable.name + ".provenance.json")
        sidecar.write_text(json.dumps({
            "build_identity": "0011223344556677",
            "binary": {"sha256": hashlib.sha256(executable.read_bytes()).hexdigest()},
        }), encoding="utf-8")
        manifest = self.write_manifest({"package": {"resources": []}})
        app = self.package(manifest)
        retained = app / "Contents/Resources/retained-from-previous-build.txt"
        retained.write_bytes(b"known-good bundle")

        executable.write_bytes(b"changed executable")
        with self.assertRaisesRegex(packager.PackageError,
                "build provenance binary sha256 does not match executable"):
            self.package(manifest)

        self.assertEqual(retained.read_bytes(), b"known-good bundle")
        sidecar.write_text(json.dumps({"binary": {"sha256": "not-a-digest"}}),
            encoding="utf-8")
        with self.assertRaisesRegex(packager.PackageError,
                "build provenance has an invalid binary sha256"):
            self.package(manifest)
        self.assertEqual(retained.read_bytes(), b"known-good bundle")

    def test_empty_resource_declaration_stages_no_project_files(self) -> None:
        shutil.rmtree(self.project / "assets")
        shutil.rmtree(self.project / "build" / "cooked")
        app = self.package(self.write_manifest({"package": {"resources": []}}))
        self.assertTrue((app / "Contents/Resources/package-provenance.json").is_file())
        self.assertEqual(self.staged(app), {
            "Game.bin", "shaders/metal/basic.cso", "shaders/elisa.shader-manifest.json"})

    def test_undeclared_resources_stage_assets_without_litter(self) -> None:
        app = self.package(self.write_manifest({}))
        self.assertEqual(self.staged(app), {
            "Game.bin", "assets/audio/step.wav", "assets/textures/trim.png",
            "assets/source/rig.blend", "build/cooked/player.pkg", "shaders/metal/basic.cso",
            "shaders/elisa.shader-manifest.json"})

    def test_compiled_shaders_only_omits_shader_sources_and_metadata(self) -> None:
        touch(self.project / "shaders" / "objectVS.hlsl", b"source")
        touch(self.project / "shaders" / "vendor" / "helper.hlsli", b"include")
        touch(self.project / "shaders" / "metal" / "nested" / "extra.cso", b"compiled")
        touch(self.project / "shaders" / "spirv" / "basic.spv", b"spirv")
        app = self.package(self.write_manifest({"package": {"resources": []}}),
            compiled_shaders_only=True)
        self.assertEqual(self.staged(app), {
            "Game.bin", "build/cooked/player.pkg", "shaders/metal/basic.cso",
            "shaders/metal/nested/extra.cso", "shaders/spirv/basic.spv",
            "shaders/elisa.shader-manifest.json"})
        manifest_path = app / "Contents/Resources/shaders/elisa.shader-manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        self.assertEqual([entry["path"] for entry in manifest["files"]], [
            "metal/basic.cso", "metal/nested/extra.cso", "spirv/basic.spv"])

    def test_compiled_shaders_only_rejects_source_only_shader_tree(self) -> None:
        (self.project / "shaders" / "metal" / "basic.cso").unlink()
        touch(self.project / "shaders" / "metal" / "basic.hlsl", b"source only")
        with self.assertRaisesRegex(packager.PackageError, "no compiled shader binaries"):
            self.package(self.write_manifest({"package": {"resources": []}}),
                compiled_shaders_only=True)
        self.assertFalse(self.output.exists())

    def test_compiled_shaders_only_rejects_symbolic_links_without_replacing_app(self) -> None:
        manifest = self.write_manifest({"package": {"resources": []}})
        existing = self.package(manifest)
        retained = existing / "Contents/Resources/retained-from-previous-build.txt"
        retained.write_bytes(b"known-good bundle")
        source = self.project / "shaders" / "metal" / "source-link.hlsl"
        os.symlink(self.project / "shaders" / "metal" / "basic.cso", source)
        with self.assertRaises(packager.PackageError):
            self.package(manifest, compiled_shaders_only=True)
        self.assertEqual(retained.read_bytes(), b"known-good bundle")

    def test_unsafe_bundle_identifier_is_rejected(self) -> None:
        with self.assertRaises(packager.PackageError):
            packager.package_app(self.project, self.project / "build/game", self.output,
                "Game", "org.elisa;touch", "1.0")

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
        manifest = self.write_manifest({"package": {"notices": [
            "third_party/SDL/LICENSE.txt", "third_party/Jolt/LICENSE.txt"]}})
        app = self.package(manifest)
        root = app / "Contents/Resources/Notices/third_party"
        self.assertEqual((root / "SDL/LICENSE.txt").read_bytes(), b"SDL notice\n")
        self.assertEqual((root / "Jolt/LICENSE.txt").read_bytes(), b"Jolt notice\n")
        package_record = json.loads((app / "Contents/Resources/package-provenance.json").read_text())
        self.assertEqual(package_record["layout"]["notices"], manifest["package"]["notices"])

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

    def test_regular_file_destination_is_refused_before_staging(self) -> None:
        self.output.parent.mkdir(parents=True)
        self.output.write_bytes(b"author file")
        with mock.patch.object(packager, "_assemble_app") as assemble:
            with self.assertRaisesRegex(packager.PackageError, "bundle destination is not a directory"):
                self.package(self.write_manifest({}))
        assemble.assert_not_called()
        self.assertEqual(self.output.read_bytes(), b"author file")
        self.assertEqual(list(self.output.parent.glob(".elisa-app-*")), [])

    def test_backup_cleanup_failure_does_not_fail_published_bundle(self) -> None:
        manifest = self.write_manifest({})
        app = self.package(manifest)
        (app / "previous-version").write_bytes(b"previous")
        rmtree = shutil.rmtree
        def leave_backup(path, *args, **kwargs):
            if Path(path).name.startswith(".elisa-app-backup-"):
                self.assertTrue(kwargs.get("ignore_errors"))
                return None  # Filesystem cleanup failure under ignore_errors.
            return rmtree(path, *args, **kwargs)
        with mock.patch.object(packager.shutil, "rmtree", side_effect=leave_backup):
            result = self.package(manifest)
        self.assertEqual(result, app)
        self.assertFalse((app / "previous-version").exists())
        self.assertEqual((app / "Contents/Resources/Game.bin").read_bytes(),
            (self.project / "build/game").read_bytes())
        backup, = app.parent.glob(".elisa-app-backup-*")
        self.assertEqual((backup / "previous-version").read_bytes(), b"previous")

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

    def test_explicit_cooked_resources_merge_with_automatic_staging(self) -> None:
        touch(self.project / "build/cooked/second.pkg", b"second package")
        for entry in ("build/cooked", "build/cooked/player.pkg"):
            with self.subTest(entry=entry):
                manifest = self.write_manifest({"package": {"resources": [entry]}})
                app = self.package(manifest)
                resources = app / "Contents/Resources"
                self.assertEqual((resources / "build/cooked/player.pkg").read_bytes(), b"x")
                self.assertEqual((resources / "build/cooked/second.pkg").read_bytes(), b"second package")
                self.assertEqual((resources / "Game.bin").read_bytes(),
                    (self.project / "build/game").read_bytes())
                self.assertFalse((resources / "assets").exists())

    def test_resource_cannot_replace_packaged_executable(self) -> None:
        app = self.package(self.write_manifest({}))
        original = (app / "Contents/Resources/Game.bin").read_bytes()
        marker = app / "previous-version"
        marker.write_bytes(b"previous")
        for name in ("Game.bin", "game.bin"):
            with self.subTest(name=name):
                touch(self.project / name, b"author resource, not executable")
                manifest = self.write_manifest({"package": {"resources": [name]}})
                with self.assertRaisesRegex(packager.PackageError,
                        "manifest resource conflicts with packaged executable"):
                    self.package(manifest)
                self.assertEqual((app / "Contents/Resources/Game.bin").read_bytes(), original)
                self.assertEqual(marker.read_bytes(), b"previous")
                self.assertEqual((self.project / name).read_bytes(), b"author resource, not executable")

    def test_resource_cannot_use_packaged_executable_as_directory(self) -> None:
        touch(self.project / "Game.bin/data", b"author resource")
        manifest = self.write_manifest({"package": {"resources": ["Game.bin/data"]}})
        with self.assertRaisesRegex(packager.PackageError,
                "manifest resource conflicts with packaged executable: Game.bin/data"):
            self.package(manifest)
        self.assertFalse(self.output.exists())

    def test_symlinked_resource_is_rejected(self) -> None:
        os.symlink(self.project / "assets" / "source", self.project / "assets" / "link")
        with self.assertRaises(packager.PackageError):
            self.package(self.write_manifest({"package": {"resources": ["assets/link"]}}))

    def test_fallback_assets_refuse_symlinks_and_preserve_previous_bundle(self) -> None:
        manifest = self.write_manifest({})
        app = self.package(manifest)
        marker = app / "previous-version"
        marker.write_bytes(b"previous")
        outside = Path(self.tempdir.name) / "outside"
        touch(outside / "payload", b"external")
        for target in (outside, outside / "payload"):
            with self.subTest(target=target):
                link = self.project / "assets/linked"
                link.symlink_to(target)
                with self.assertRaisesRegex(packager.PackageError,
                        "package directory contains a symbolic link"):
                    self.package(manifest)
                self.assertEqual(marker.read_bytes(), b"previous")
                self.assertFalse((app / "Contents/Resources/assets/linked").exists())
                link.unlink()

    def test_ignored_symlink_litter_is_not_followed(self) -> None:
        shutil.rmtree(self.project / "assets/.git")
        (self.project / "assets/.git").symlink_to(Path(self.tempdir.name) / "missing")
        app = self.package(self.write_manifest({}))
        self.assertFalse((app / "Contents/Resources/assets/.git").exists())

    def test_resource_parent_symlink_is_refused_and_preserves_bundle(self) -> None:
        app = self.package(self.write_manifest({}))
        marker = app / "previous-version"
        marker.write_bytes(b"previous")
        external = Path(self.tempdir.name) / "outside"
        touch(external / "payload", b"outside project")
        for target in (external, self.project / "assets"):
            with self.subTest(target=target):
                link = self.project / "linked"
                link.symlink_to(target, target_is_directory=True)
                leaf = "payload" if target == external else "audio/step.wav"
                manifest = self.write_manifest({"package": {"resources": [f"linked/{leaf}"]}})
                with self.assertRaisesRegex(packager.PackageError,
                        "manifest resource must not be a symbolic link: linked"):
                    self.package(manifest)
                self.assertEqual(marker.read_bytes(), b"previous")
                self.assertFalse((app / "Contents/Resources/linked").exists())
                self.assertEqual((external / "payload").read_bytes(), b"outside project")
                link.unlink()

    def test_resource_directory_with_nested_symlink_is_rejected_with_path(self) -> None:
        external = Path(self.tempdir.name) / "shared.wav"
        touch(external, b"audio")
        nested = self.project / "assets" / "audio" / "shared.wav"
        os.symlink(external, nested)
        manifest = self.write_manifest({"package": {"resources": ["assets/audio"]}})
        with self.assertRaisesRegex(packager.PackageError,
                r"manifest resource contains a symbolic link: assets/audio/shared\.wav"):
            self.package(manifest)
        self.assertFalse(self.output.exists())

if __name__ == "__main__":
    unittest.main()
