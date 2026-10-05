#!/usr/bin/env python3
"""Regression checks for safe relocated app launches and teardown."""

from __future__ import annotations

import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from validate_interactive_macos_app import (
    launch_environment,
    process_group_exists,
    stop_process_group,
    stop_process_group_gracefully,
)


def start_wrapped_runtime(runtime: Path, source: str) -> subprocess.Popen[bytes]:
    runtime.write_text(f"#!{sys.executable}\n{source}", encoding="utf-8")
    runtime.chmod(0o755)
    launcher = runtime.with_name("launcher.sh")
    launcher.write_text(
        "#!/bin/sh\n"
        f"\"{runtime}\" &\n"
        "child=$!\n"
        "wait \"$child\"\n"
        "exit $?\n",
        encoding="utf-8",
    )
    launcher.chmod(0o755)
    return subprocess.Popen([str(launcher)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        start_new_session=True)


class InteractiveMacosAppTests(unittest.TestCase):
    def test_launch_environment_forces_audio_device_unavailable(self) -> None:
        environment = launch_environment(Path("/tmp/home"), Path("/tmp/user-data"), {
            "ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE": "0",
            "ELISA_INHERITED_SETTING": "discard",
            "WICKED_SHADER_PATH": "/build/shaders",
            "DYLD_LIBRARY_PATH": "/build/libs",
            "PATH": "/custom/bin",
            "LANG": "en_US.UTF-8",
        })

        self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], "1")
        self.assertEqual(environment["ELISA_USER_DATA_DIR"], "/tmp/user-data")
        self.assertEqual(environment["HOME"], "/tmp/home")
        self.assertEqual(environment["PATH"], "/usr/bin:/bin")
        self.assertEqual(environment["LANG"], "en_US.UTF-8")
        self.assertNotIn("ELISA_INHERITED_SETTING", environment)
        self.assertNotIn("WICKED_SHADER_PATH", environment)
        self.assertNotIn("DYLD_LIBRARY_PATH", environment)

    def test_waits_for_a_child_after_the_launcher_exits(self) -> None:
        process = subprocess.Popen(
            ["/bin/sh", "-c", "(trap '' TERM; sleep 0.3) & exit 0"],
            start_new_session=True,
        )
        self.assertEqual(process.wait(timeout=2), 0)
        self.assertTrue(process_group_exists(process.pid))

        started = time.monotonic()
        self.assertEqual(stop_process_group(process), 0)

        self.assertGreaterEqual(time.monotonic() - started, 0.2)
        self.assertFalse(process_group_exists(process.pid))

    def test_graceful_stop_requires_zero_exit_and_reaps_process_group(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder) / "Game Runtime.bin"
            process = start_wrapped_runtime(runtime,
                "import signal, sys, time\n"
                "signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))\n"
                "print('ready', flush=True)\n"
                "time.sleep(30)\n")
            assert process.stdout is not None
            try:
                with process.stdout:
                    self.assertEqual(process.stdout.readline(), b"ready\n")

                self.assertEqual(stop_process_group_gracefully(process, runtime), 0)
                self.assertFalse(process_group_exists(process.pid))
            finally:
                if process_group_exists(process.pid):
                    stop_process_group(process, force=True)

    def test_graceful_stop_rejects_default_signal_termination(self) -> None:
        with tempfile.TemporaryDirectory() as folder:
            runtime = Path(folder) / "Game Runtime.bin"
            process = start_wrapped_runtime(runtime,
                "import time\nprint('ready', flush=True)\ntime.sleep(30)\n")
            assert process.stdout is not None
            try:
                with process.stdout:
                    self.assertEqual(process.stdout.readline(), b"ready\n")

                with self.assertRaisesRegex(ValueError, "did not exit cleanly after runtime SIGTERM"):
                    stop_process_group_gracefully(process, runtime)
                self.assertEqual(process.returncode, 143)
                self.assertFalse(process_group_exists(process.pid))
            finally:
                if process_group_exists(process.pid):
                    stop_process_group(process, force=True)


if __name__ == "__main__":
    unittest.main()
