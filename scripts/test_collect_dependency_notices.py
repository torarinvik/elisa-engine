import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from collect_dependency_notices import collect


class NoticeCollectionTests(unittest.TestCase):
    def test_extra_catalog_is_hash_verified_and_recorded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            base_notice = b"engine notice\n"
            font_notice = b"font license\n"
            (root / "ENGINE.txt").write_bytes(base_notice)
            (root / "FONT.txt").write_bytes(font_notice)
            base = root / "base.json"
            base.write_text(json.dumps({"schema": 1, "complete": False, "sources": [{
                "name": "Engine", "root": "engine", "path": "ENGINE.txt",
                "sha256": hashlib.sha256(base_notice).hexdigest()}]}))
            extra = root / "font.json"
            extra.write_text(json.dumps({"schema": 1, "complete": True, "sources": [{
                "name": "Font", "root": "engine", "path": "FONT.txt",
                "sha256": hashlib.sha256(font_notice).hexdigest()}]}))
            output = root / "notices"
            self.assertEqual(collect(base, {"engine": root}, output, (extra,)), 2)
            self.assertEqual((output / "Font.txt").read_bytes(), font_notice)
            merged = json.loads((output / "sources.json").read_text())
            self.assertEqual([entry["name"] for entry in merged["sources"]], ["Engine", "Font"])
            self.assertFalse(merged["complete"])

    def test_extra_catalog_rejects_changed_source_without_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "ENGINE.txt").write_bytes(b"engine notice")
            (root / "FONT.txt").write_bytes(b"changed font license")
            base = root / "base.json"
            base.write_text(json.dumps({"schema": 1, "sources": []}))
            extra = root / "font.json"
            extra.write_text(json.dumps({"schema": 1, "sources": [{
                "name": "Font", "root": "engine", "path": "FONT.txt",
                "sha256": hashlib.sha256(b"expected font license").hexdigest()}]}))
            output = root / "notices"
            with self.assertRaisesRegex(ValueError, "source changed"):
                collect(base, {"engine": root}, output, (extra,))
            self.assertFalse(output.exists())

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

    def test_excerpts_are_bounded_and_independently_verified(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = b"code\n/* notice */\ncode"
            notice = b"/* notice */"
            (root / "source.h").write_bytes(data)
            entry = {"name": "Embedded", "root": "engine", "path": "source.h",
                "sha256": hashlib.sha256(data).hexdigest(), "excerpt": {
                    "offset": 5, "bytes": len(notice), "sha256": hashlib.sha256(notice).hexdigest()}}
            manifest = root / "catalog.json"
            def write():
                manifest.write_text(json.dumps({"schema": 1, "sources": [entry]}))
            write()
            self.assertEqual(collect(manifest, {"engine": root}, root / "valid"), 1)
            self.assertEqual((root / "valid/Embedded.txt").read_bytes(), notice)
            for offset in (-1, 999, 4):
                entry["excerpt"]["offset"] = offset
                write()
                with self.assertRaises(ValueError):
                    collect(manifest, {"engine": root}, root / "invalid")
                self.assertFalse((root / "invalid").exists())
