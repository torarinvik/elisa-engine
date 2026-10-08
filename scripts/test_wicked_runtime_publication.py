"""Fault controls for executable and Wicked runtime publication."""
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import wicked_runtime as runtime


class RuntimePublicationTests(unittest.TestCase):
    def setup_paths(self, root):
        output = root / "game"
        output.write_bytes(b"old executable")
        candidate = root / "candidate"
        candidate.write_bytes(b"new executable")
        source = root / "source"
        source.mkdir()
        for name in runtime.RUNTIME_LIBRARIES:
            (source / name).write_bytes(b"library")
            (root / name).symlink_to("previous/" + name)
        return output, candidate, source

    def assert_old(self, root, output, candidate):
        self.assertEqual(output.read_bytes(), b"old executable")
        self.assertEqual(candidate.read_bytes(), b"new executable")
        for name in runtime.RUNTIME_LIBRARIES:
            self.assertEqual(os.readlink(root / name), "previous/" + name)

    def test_second_link_failure_restores_first(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, candidate, source = self.setup_paths(root)
            replace = os.replace
            def fail_second(src, dst):
                if Path(src).name == "next-1":
                    raise OSError("second link refused")
                return replace(src, dst)
            with mock.patch.object(runtime.os, "replace", side_effect=fail_second):
                with self.assertRaisesRegex(runtime.WickedRuntimeError, "second link refused"):
                    runtime.stage_wicked_runtime_libraries(output, source, staged_executable=candidate)
            self.assert_old(root, output, candidate)
            self.assertEqual(list(root.glob(".wicked-runtime-*")), [])

    def test_executable_failure_restores_removed_optional_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, candidate, source = self.setup_paths(root)
            (source / runtime.RUNTIME_LIBRARIES[1]).unlink()
            replace = os.replace
            def fail_executable(src, dst):
                if Path(src) == candidate:
                    raise OSError("executable refused")
                return replace(src, dst)
            with mock.patch.object(runtime.os, "replace", side_effect=fail_executable):
                with self.assertRaisesRegex(runtime.WickedRuntimeError, "executable refused"):
                    runtime.stage_wicked_runtime_libraries(output, source, staged_executable=candidate)
            self.assert_old(root, output, candidate)

    def test_second_collision_does_not_change_first_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, candidate, source = self.setup_paths(root)
            second = root / runtime.RUNTIME_LIBRARIES[1]
            second.unlink()
            second.write_bytes(b"author data")
            with self.assertRaisesRegex(runtime.WickedRuntimeError, "refusing to replace"):
                runtime.stage_wicked_runtime_libraries(output, source, staged_executable=candidate)
            self.assertEqual(os.readlink(root / runtime.RUNTIME_LIBRARIES[0]), "previous/" + runtime.RUNTIME_LIBRARIES[0])
            self.assertEqual(second.read_bytes(), b"author data")
            self.assertEqual(output.read_bytes(), b"old executable")

    def test_failed_rollback_retains_previous_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, candidate, source = self.setup_paths(root)
            replace = os.replace
            def fail_publication_and_rollback(src, dst):
                if Path(src) == candidate or Path(src).name == "previous-0":
                    raise OSError("injected refusal")
                return replace(src, dst)
            with mock.patch.object(runtime.os, "replace", side_effect=fail_publication_and_rollback):
                with self.assertRaisesRegex(runtime.WickedRuntimeError, "recovery retained"):
                    runtime.stage_wicked_runtime_libraries(output, source, staged_executable=candidate)
            self.assertEqual(output.read_bytes(), b"old executable")
            recovery, = root.glob(".wicked-runtime-*")
            self.assertEqual(os.readlink(recovery / "previous-0"), "previous/" + runtime.RUNTIME_LIBRARIES[0])
            self.assertTrue((recovery / "recovery.json").is_file())

    def test_provenance_transaction_failures(self):
        for failure in ("sidecar", "executable"):
            for previous in ("file", "symlink", "absent"):
                with self.subTest(failure=failure, previous=previous), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary)
                    output, candidate, source = self.setup_paths(root)
                    sidecar = root / "game.provenance.json"
                    if previous == "file":
                        sidecar.write_bytes(b"old manifest")
                    elif previous == "symlink":
                        sidecar.symlink_to("previous/manifest.json")
                    staged = root / "candidate.json"
                    staged.write_bytes(b"new manifest")
                    replace = os.replace
                    def fail(src, dst):
                        if Path(src) == (staged if failure == "sidecar" else candidate):
                            raise OSError("publication refused")
                        return replace(src, dst)
                    with mock.patch.object(runtime.os, "replace", side_effect=fail):
                        with self.assertRaisesRegex(runtime.WickedRuntimeError, "publication refused"):
                            runtime.stage_wicked_runtime_libraries(output, source,
                                staged_executable=candidate, staged_provenance=staged)
                    self.assert_old(root, output, candidate)
                    if previous == "file":
                        self.assertEqual(sidecar.read_bytes(), b"old manifest")
                    elif previous == "symlink":
                        self.assertEqual(os.readlink(sidecar), "previous/manifest.json")
                    else:
                        self.assertFalse(sidecar.exists())
                    self.assertEqual(list(root.glob(".wicked-runtime-*")), [])

    def test_invalid_provenance_is_refused_before_publication(self):
        for invalid in ("missing", "destination-directory"):
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                output, candidate, source = self.setup_paths(root)
                staged = root / "candidate.json"
                if invalid == "destination-directory":
                    staged.write_bytes(b"new manifest")
                    (root / "game.provenance.json").mkdir()
                with self.assertRaises(runtime.WickedRuntimeError):
                    runtime.stage_wicked_runtime_libraries(output, source,
                        staged_executable=candidate, staged_provenance=staged)
                self.assert_old(root, output, candidate)

    def test_provenance_is_published_before_executable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, candidate, source = self.setup_paths(root)
            staged = root / "candidate.json"
            staged.write_bytes(b"new manifest")
            replace = os.replace
            def observe(src, dst):
                if Path(src) == candidate:
                    self.assertEqual((root / "game.provenance.json").read_bytes(), b"new manifest")
                    for name in runtime.RUNTIME_LIBRARIES:
                        self.assertEqual((root / name).resolve(), (source / name).resolve())
                return replace(src, dst)
            with mock.patch.object(runtime.os, "replace", side_effect=observe):
                runtime.stage_wicked_runtime_libraries(output, source,
                    staged_executable=candidate, staged_provenance=staged)
            self.assertEqual(output.read_bytes(), b"new executable")

    def test_success_publishes_executable_after_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output, candidate, source = self.setup_paths(root)
            staged = runtime.stage_wicked_runtime_libraries(output, source, staged_executable=candidate)
            self.assertEqual(len(staged), 2)
            self.assertEqual(output.read_bytes(), b"new executable")
            self.assertFalse(candidate.exists())
            for name in runtime.RUNTIME_LIBRARIES:
                self.assertEqual((root / name).resolve(), (source / name).resolve())


if __name__ == "__main__":
    unittest.main()
