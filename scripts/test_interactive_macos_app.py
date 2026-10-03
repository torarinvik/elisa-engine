#!/usr/bin/env python3
"""Regression checks for stopping the relocated app's complete process group."""

from __future__ import annotations

import subprocess
import time
import unittest

from validate_interactive_macos_app import process_group_exists, stop_process_group


class InteractiveMacosAppTests(unittest.TestCase):
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
