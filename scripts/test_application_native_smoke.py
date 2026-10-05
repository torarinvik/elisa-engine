#!/usr/bin/env python3
"""Keep automated application launches silent even when the user has audio enabled."""

from __future__ import annotations

import unittest

from application_native_smoke import native_smoke_environment


class ApplicationNativeSmokeTests(unittest.TestCase):
    def test_launch_environment_forces_silent_audio_provider(self) -> None:
        environment = native_smoke_environment({
            "ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE": "0",
            "ELISA_PROJECT_ROOT": "/tmp/project",
            "PATH": "/usr/bin",
        })

        self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], "1")
        self.assertEqual(environment["ELISA_PROJECT_ROOT"], "/tmp/project")
        self.assertEqual(environment["PATH"], "/usr/bin")


if __name__ == "__main__":
    unittest.main()
