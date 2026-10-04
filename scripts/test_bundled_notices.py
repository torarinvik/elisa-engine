import hashlib
from pathlib import Path
import tempfile
import unittest

from check_bundled_notices import audit


class BundledNoticeTests(unittest.TestCase):
    def test_detects_missing_tampered_and_unmapped_notices(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder)
            libraries = app / "Contents/Frameworks"
            notices = app / "Contents/Resources/Notices"
            libraries.mkdir(parents=True)
            notices.mkdir(parents=True)
            (libraries / "libexample.dylib").write_bytes(b"fixture")
            catalog = {"sources": [{"name": "Example", "sha256": hashlib.sha256(b"notice").hexdigest()}],
                "complete": False, "statically_linked_components": {
                    "Example static": {"evidence": "libexample.a", "notices": ["Example"],
                        "remaining": ["one unreviewed source"]}},
                "bundled_libraries": {"libexample.dylib": {"notices": ["Example"]}}}
            initial = audit(app, catalog)
            self.assertFalse(initial["dylib_notice_files_verified"])
            self.assertFalse(initial["statically_linked_notice_files_verified"])
            self.assertTrue(initial["bundled_resource_notice_files_verified"])
            catalog["statically_linked_components"]["Example static"]["notices"] = []
            self.assertFalse(audit(app, catalog)["statically_linked_notice_files_verified"])
            catalog["statically_linked_components"]["Example static"]["notices"] = ["Example"]
            (notices / "Example.txt").write_bytes(b"notice")
            report = audit(app, catalog)
            self.assertTrue(report["dylib_notice_files_verified"])
            self.assertTrue(report["statically_linked_notice_files_verified"])
            self.assertEqual(report["statically_linked_components"][0]["remaining"],
                ["one unreviewed source"])
            self.assertFalse(report["catalog_complete"])
            (notices / "Example.txt").write_bytes(b"modified")
            report = audit(app, catalog)
            self.assertFalse(report["dylib_notice_files_verified"])
            self.assertFalse(report["statically_linked_notice_files_verified"])
            (notices / "Example.txt").write_bytes(b"notice")
            (libraries / "libunknown.dylib").write_bytes(b"new library")
            self.assertFalse(audit(app, catalog)["dylib_notice_files_verified"])

    def test_audits_bundled_resource_tree_and_rejects_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / "Example.app"
            shader_dir = app / "Contents/Resources/shaders"
            notices = app / "Contents/Resources/Notices"
            shader_dir.mkdir(parents=True)
            notices.mkdir(parents=True)
            (shader_dir / "blockcompressCS_BC1.hlsl").write_text("shader")
            (notices / "Example.txt").write_bytes(b"notice")
            catalog = {"complete": False, "sources": [{"name": "Example",
                "sha256": hashlib.sha256(b"notice").hexdigest()}], "bundled_resources": {
                    "Wicked shader library": {"path": "Contents/Resources/shaders",
                        "evidence": "Packager stages the prepared shader folder",
                        "notices": ["Example"], "remaining": ["other shaders under review"]}}}
            report = audit(app, catalog)
            self.assertTrue(report["bundled_resource_notice_files_verified"])
            self.assertEqual(report["bundled_resources"][0]["remaining"],
                ["other shaders under review"])
            catalog["bundled_resources"]["Wicked shader library"]["path"] = "../outside"
            self.assertFalse(audit(app, catalog)["bundled_resource_notice_files_verified"])
            catalog["bundled_resources"]["Wicked shader library"]["path"] = "Contents/Resources/missing"
            self.assertFalse(audit(app, catalog)["bundled_resource_notice_files_verified"])
