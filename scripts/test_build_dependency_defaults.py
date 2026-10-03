#!/usr/bin/env python3
"""Dependency default selection must follow the pin, with explicit overrides preserved."""
import json, os, tempfile, unittest
from pathlib import Path
from unittest import mock
import elisa_build_run as runner
class DependencyDefaultTests(unittest.TestCase):
    def test_default_wicked_root_follows_dependency_manifest(self) -> None:
        with tempfile.TemporaryDirectory(prefix="Elisa pinned dependency ") as temporary_directory:
            root = Path(temporary_directory)
            engine = root / "engine"
            (engine / "native").mkdir(parents=True)
            manifest = engine / "native/dependency-manifest.json"
            manifest.write_text(json.dumps({"libraries": [{"name": "WickedEngine", "path_default": "../pinned wicked"}]}))
            args = runner.parse_arguments(["build", "--project", str(root), "--brew-prefix", str(root), "--sdl3-root", str(root)])
            with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(runner, "ENGINE_ROOT", engine):
                self.assertEqual(runner.resolve_native_paths(args)["wicked_root"], (root / "pinned wicked").resolve())
                args.wicked_root = str(root / "explicit wicked")
                manifest.write_text("invalid")
                self.assertEqual(runner.resolve_native_paths(args)["wicked_root"], (root / "explicit wicked").resolve())
                args.wicked_root = None
                with self.assertRaises(runner.BuildConfigurationError):
                    runner.resolve_native_paths(args)


if __name__ == '__main__':
    unittest.main()
