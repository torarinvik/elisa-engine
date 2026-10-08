"""Cook publication restores old/absent outputs on filesystem failures."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from cook_publication import CookPublicationError, publish_cooked_outputs


class CookPublicationTests(unittest.TestCase):
    def test_second_replace_failure_restores_first_output(self):
        for previous in (True, False):
            with self.subTest(previous=previous), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                first, second = root / "mesh.pkg", root / "texture.png"
                if previous:
                    first.write_bytes(b"old mesh")
                    first.chmod(0o640)
                second.write_bytes(b"old texture")
                staged = {first: root / "new-mesh", second: root / "new-texture"}
                for temporary in staged.values():
                    temporary.write_bytes(b"new data")
                real_replace = os.replace

                def replace(source, destination):
                    if Path(source) == staged[second]:
                        raise PermissionError("injected texture publication failure")
                    return real_replace(source, destination)

                with mock.patch("cook_publication.os.replace", side_effect=replace):
                    with self.assertRaisesRegex(CookPublicationError, "previous outputs restored"):
                        publish_cooked_outputs(staged)
                self.assertEqual(second.read_bytes(), b"old texture")
                if previous:
                    self.assertEqual(first.read_bytes(), b"old mesh")
                    self.assertEqual(first.stat().st_mode & 0o777, 0o640)
                else:
                    self.assertFalse(first.exists())
                self.assertFalse(list(root.glob(".*.elisa-backup-*")))

    def test_success_publishes_all_and_cleans_backups(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            final, temporary = root / "mesh", root / "new"
            final.write_bytes(b"old")
            temporary.write_bytes(b"new")
            temporary.chmod(0o755)
            publish_cooked_outputs({final: temporary})
            self.assertEqual(final.read_bytes(), b"new")
            self.assertEqual(final.stat().st_mode & 0o777, 0o755)
            self.assertEqual(list(root.iterdir()), [final])

    def test_failed_rollback_keeps_recovery_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "mesh", root / "texture"
            first.write_bytes(b"old mesh")
            second.write_bytes(b"old texture")
            staged = {first: root / "new-mesh", second: root / "new-texture"}
            for temporary in staged.values():
                temporary.write_bytes(b"new")
            real_replace = os.replace

            def replace(source, destination):
                if Path(source) != staged[first]:
                    raise PermissionError("injected publication/restore failure")
                return real_replace(source, destination)

            with mock.patch("cook_publication.os.replace", side_effect=replace):
                with self.assertRaisesRegex(CookPublicationError, "rollback failed"):
                    publish_cooked_outputs(staged)
            backups = list(root.glob(".mesh.elisa-backup-*"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), b"old mesh")
            self.assertEqual(second.read_bytes(), b"old texture")


if __name__ == "__main__":
    unittest.main()
