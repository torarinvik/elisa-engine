"""Failure-injection coverage for shader library publication."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import shader_library_publish as publisher


class ShaderPublicationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "shaders"
        (self.root / "metal").mkdir(parents=True)
        (self.root / "metal/old.cso").write_bytes(b"old shader")
        (self.root / publisher.SHADER_MANIFEST_NAME).write_bytes(b"original manifest")
        self.compiled = self.base / "compiled"
        self.compiled.mkdir()
        self.binary = self.compiled / "new.cso"
        self.binary.write_bytes(b"new shader")

    def assert_original(self):
        self.assertEqual((self.root / "metal/old.cso").read_bytes(), b"old shader")
        self.assertEqual((self.root / publisher.SHADER_MANIFEST_NAME).read_bytes(), b"original manifest")
        self.assertFalse((self.root / "metal/new.cso").exists())

    def test_copy_failure_preserves_original(self):
        with mock.patch.object(publisher.shutil, "copyfile", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                publisher.publish(self.root, self.compiled, [self.binary])
        self.assert_original()

    def test_manifest_failure_preserves_original(self):
        with mock.patch.object(publisher, "shader_manifest", side_effect=publisher.PackageError("invalid")):
            with self.assertRaises(publisher.PackageError):
                publisher.publish(self.root, self.compiled, [self.binary])
        self.assert_original()

    def test_publish_failure_rolls_back(self):
        replace = os.replace
        def fail_stage(source, destination):
            if Path(source).parent.name.startswith(".elisa-shader-stage-"):
                raise OSError("publish failed")
            return replace(source, destination)
        with mock.patch.object(publisher.os, "replace", side_effect=fail_stage):
            with self.assertRaises(OSError):
                publisher.publish(self.root, self.compiled, [self.binary])
        self.assert_original()
        self.assertEqual(list(self.base.glob(".elisa-shader-backup-*")), [])

    def test_nested_symlink_is_rejected_without_touching_target(self):
        outside = self.base / "outside"
        outside.write_bytes(b"private")
        (self.root / "metal/link.cso").symlink_to(outside)
        with self.assertRaises(publisher.PackageError):
            publisher.publish(self.root, self.compiled, [self.binary])
        self.assert_original()
        self.assertEqual(outside.read_bytes(), b"private")

    def test_rollback_failure_retains_backup(self):
        replace = os.replace
        def fail_after_backup(source, destination):
            if Path(source) != self.root:
                raise OSError("replacement unavailable")
            return replace(source, destination)
        with mock.patch.object(publisher.os, "replace", side_effect=fail_after_backup):
            with self.assertRaisesRegex(publisher.PackageError, "previous library retained at"):
                publisher.publish(self.root, self.compiled, [self.binary])
        backups = list(self.base.glob(".elisa-shader-backup-*"))
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / "metal/old.cso").read_bytes(), b"old shader")
        self.assertEqual((backups[0] / publisher.SHADER_MANIFEST_NAME).read_bytes(), b"original manifest")
