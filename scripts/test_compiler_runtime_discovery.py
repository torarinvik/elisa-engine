"""Runtime identity checks for installed compiler launchers."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


def touch(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")


class RuntimeDiscoveryTests(unittest.TestCase):
    def test_installed_launcher_uses_its_snapshot_runtime(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Elisa installed snapshot ") as temporary:
            root = Path(temporary)
            compiler = root / "elisac-stage1"
            launcher = root / "stage1/scripts/elisac_stage1.sh"
            runtime = root / "stage1/build/runtime/elisacore_runtime.o"
            touch(launcher)
            touch(runtime)
            touch(root / "build/runtime/elisacore_runtime.o")
            compiler.write_text(f'#!/bin/bash\nexec bash "{launcher}" "$@"\n')
            with mock.patch.dict(os.environ, {}, clear=True):
                self.assertEqual(runner.resolve_runtime_object(None, str(compiler)), runtime.resolve())
                runtime.unlink()
                with self.assertRaises(runner.BuildConfigurationError):
                    runner.resolve_runtime_object(None, str(compiler))

    def test_runtime_discovery_does_not_execute_wrapper_commands(self) -> None:
        runner = __import__("elisa_build_run")
        with tempfile.TemporaryDirectory(prefix="Elisa wrapper validation ") as temporary:
            root = Path(temporary)
            compiler = root / "elisac-stage1"
            marker = root / "executed"
            compiler.write_text(f'#!/bin/bash\ntouch "{marker}"\nexec bash /missing "$@"\n')
            with mock.patch.dict(os.environ, {}, clear=True):
                with self.assertRaises(runner.BuildConfigurationError):
                    runner.resolve_runtime_object(None, str(compiler))
            self.assertFalse(marker.exists())

