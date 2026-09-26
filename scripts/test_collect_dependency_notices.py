import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from collect_dependency_notices import collect


class NoticeCollectionTests(unittest.TestCase):
    def test_exact_bytes_and_source_drift(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = b"Original notice\r\n"
            (root / "LICENSE").write_bytes(data)
            manifest = root / "catalog.json"
            manifest.write_text(json.dumps({"schema": 1, "complete": False, "sources": [{
                "name": "Example", "root": "engine", "path": "LICENSE",
                "sha256": hashlib.sha256(data).hexdigest()}]}))
            output = root / "output"
            self.assertEqual(collect(manifest, {"engine": root}, output), 1)
            self.assertEqual((output / "Example.txt").read_bytes(), data)
            (root / "LICENSE").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "source changed"):
                collect(manifest, {"engine": root}, root / "failed")
            self.assertFalse((root / "failed").exists())
            self.assertEqual((output / "Example.txt").read_bytes(), data)

    def test_source_escape_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source_root = root / "source"
            source_root.mkdir()
            (root / "LICENSE").write_bytes(b"notice")
            manifest = root / "catalog.json"
            manifest.write_text(json.dumps({"schema": 1, "sources": [{"name": "Escape",
                "root": "engine", "path": "../LICENSE", "sha256": "0" * 64}]}))
            with self.assertRaisesRegex(ValueError, "escapes root"):
                collect(manifest, {"engine": source_root}, root / "output")
