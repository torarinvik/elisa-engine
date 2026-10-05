#!/usr/bin/env python3
"""Regression tests for native gate runner provenance."""

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

from write_native_gate_report import elisascript_identity


class ElisaScriptIdentityTests(unittest.TestCase):
    def test_records_configured_executable_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "elisascript"
            executable.write_bytes(b"stale product")
            with patch.dict(os.environ, {"ELISASCRIPT_BIN": str(executable)}):
                identity = elisascript_identity()

        self.assertEqual(identity["configured"], str(executable))
        self.assertEqual(identity["executable"]["path"], str(executable.resolve()))
        self.assertEqual(
            identity["executable"]["sha256"],
            "6422927783f6e2a6df797bc690629c20f038c470f103985a8f68612ebff777dd",
        )

    def test_missing_selected_executable_is_explicit(self) -> None:
        with patch.dict(os.environ, {"ELISASCRIPT_BIN": "/missing/elisascript"}):
            identity = elisascript_identity()

        self.assertEqual(identity["configured"], "/missing/elisascript")
        self.assertIsNone(identity["executable"])


if __name__ == "__main__":
    unittest.main()
