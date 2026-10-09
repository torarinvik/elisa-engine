"""Focused checks for the generated macOS application launcher."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import unittest
from pathlib import Path

from package_test_support import PackageAppTestSupport, touch


class PackageMacosLauncherTests(PackageAppTestSupport, unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
