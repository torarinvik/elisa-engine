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
                "complete": False, "bundled_libraries": {"libexample.dylib": {"notices": ["Example"]}}}
            self.assertFalse(audit(app, catalog)["dylib_notice_files_verified"])
            (notices / "Example.txt").write_bytes(b"notice")
            report = audit(app, catalog)
            self.assertTrue(report["dylib_notice_files_verified"])
            self.assertFalse(report["catalog_complete"])
            (notices / "Example.txt").write_bytes(b"modified")
            self.assertFalse(audit(app, catalog)["dylib_notice_files_verified"])
            (notices / "Example.txt").write_bytes(b"notice")
            (libraries / "libunknown.dylib").write_bytes(b"new library")
            self.assertFalse(audit(app, catalog)["dylib_notice_files_verified"])
