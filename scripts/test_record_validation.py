#!/usr/bin/env python3
"""Tests for the compiler identity recorded in build/validation.json."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from record_validation import compiler_product_path


class CompilerProductPathTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        (root / "scripts").mkdir()
        (root / "bin").mkdir()
        self.wrapper = root / "scripts/elisac_stage1.sh"
        self.wrapper.write_text("#!/usr/bin/env bash\n", encoding="utf-8")
        self.product = root / "bin/elisac-stage1"
        self.product.write_bytes(b"product")
        self.pinned = root / "pinned-stage1"
        self.pinned.write_bytes(b"pinned")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_wrapper_records_checkout_product(self) -> None:
        self.assertEqual(compiler_product_path(str(self.wrapper), {}), self.product.resolve())

    def test_wrapper_records_pinned_override(self) -> None:
        environ = {"ELISA_STAGE1_BIN": str(self.pinned)}
        self.assertEqual(compiler_product_path(str(self.wrapper), environ), self.pinned.resolve())

    def test_direct_product_is_recorded_as_is(self) -> None:
        environ = {"ELISA_STAGE1_BIN": str(self.pinned)}
        self.assertEqual(compiler_product_path(str(self.product), environ), self.product.resolve())

    def test_missing_override_fails(self) -> None:
        with self.assertRaises(FileNotFoundError):
            compiler_product_path(str(self.wrapper), {"ELISA_STAGE1_BIN": str(self.pinned) + "-gone"})


if __name__ == "__main__":
    unittest.main()
