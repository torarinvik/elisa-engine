#!/usr/bin/env python3
"""Tests for the compiler identity and proof reports recorded in build/validation.json."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from record_validation import compiler_product_path, verified_proofs


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


def proof_report(obligations: int, proven: int) -> str:
    return json.dumps({
        "status": "proved" if proven == obligations else "failed",
        "verification_state": "proved" if proven == obligations else "unknown",
        "summary": {"obligations": obligations, "proven": proven, "failed": obligations - proven,
                    "semantic_diagnostics": 0, "semantic_errors": 0},
        "replay": {"replayed": proven, "gaps": 0},
        "kernel": {"independent_replay": True},
    })


class VerifiedProofsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "proof").mkdir()
        (self.root / "build").mkdir()
        for name in ("entity_id", "audio_virtual"):
            (self.root / f"proof/{name}.elisa").write_text("", encoding="utf-8")
        (self.root / "proof/notes.txt").write_text("", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_every_proof_report_is_recorded(self) -> None:
        (self.root / "build/entity-id-proof.json").write_text(proof_report(3, 3), encoding="utf-8")
        (self.root / "build/audio-virtual-proof.json").write_text(proof_report(5, 5), encoding="utf-8")
        proofs = verified_proofs(self.root)
        self.assertEqual(sorted(proofs), ["audio_virtual", "entity_id"])
        self.assertEqual(proofs["audio_virtual"]["obligations"], 5)

    def test_unproved_report_fails(self) -> None:
        (self.root / "build/entity-id-proof.json").write_text(proof_report(3, 3), encoding="utf-8")
        (self.root / "build/audio-virtual-proof.json").write_text(proof_report(5, 4), encoding="utf-8")
        with self.assertRaises(ValueError):
            verified_proofs(self.root)

    def test_missing_report_fails(self) -> None:
        (self.root / "build/entity-id-proof.json").write_text(proof_report(3, 3), encoding="utf-8")
        with self.assertRaises(FileNotFoundError):
            verified_proofs(self.root)

    def test_no_proofs_fails(self) -> None:
        for source in (self.root / "proof").glob("*.elisa"):
            source.unlink()
        with self.assertRaises(ValueError):
            verified_proofs(self.root)


if __name__ == "__main__":
    unittest.main()
