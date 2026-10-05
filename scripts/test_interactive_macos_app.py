#!/usr/bin/env python3
"""Regression checks for safe relocated app launches and teardown."""

from __future__ import annotations

import subprocess
import time
import unittest
from pathlib import Path

from validate_interactive_macos_app import (
    launch_environment,
    process_group_exists,
    stop_process_group,
)


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


if __name__ == "__main__":
    unittest.main()
