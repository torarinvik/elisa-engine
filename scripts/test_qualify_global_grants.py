"""Grant qualification fails closed on silent acceptance and unrelated errors."""
from pathlib import Path
import subprocess
import hashlib
import json
import tempfile
import unittest
from unittest import mock

import qualify_global_grants as gate


class GlobalGrantQualificationTests(unittest.TestCase):
    def run_controls(self, responder):
        with tempfile.TemporaryDirectory() as temporary, mock.patch.object(gate.subprocess, "run", side_effect=responder):
            return gate.qualify("candidate", Path(temporary))

    def test_silent_acceptance_fails_missing_grants(self):
        rows = self.run_controls(lambda *args, **kwargs: subprocess.CompletedProcess(args, 0, "", ""))
        self.assertEqual(len(rows), 22)
        self.assertTrue(any(not row["passed"] and not row["expected_accept"] for row in rows))

    def test_unrelated_failure_is_not_grant_evidence(self):
        rows = self.run_controls(lambda *args, **kwargs: subprocess.CompletedProcess(args, 1, "", "parse error"))
        self.assertFalse(any(row["passed"] for row in rows))

    def test_exact_default_policy_and_bypass_pass(self):
        permissions = {name: permission for name, _, permission in gate.CASES}

        def respond(command, **kwargs):
            permission = permissions[Path(command[-1]).stem]
            denied = permission and "-permissive" not in command
            return subprocess.CompletedProcess(command, 1 if denied else 0, "",
                f"mutable global requires {permission}" if denied else "")

        self.assertTrue(all(row["passed"] for row in self.run_controls(respond)))

    def test_transitive_refusal_wording_preserves_bypass_requirement(self):
        permissions = {name: permission for name, _, permission in gate.CASES}

        def respond(command, **kwargs):
            permission = permissions[Path(command[-1]).stem]
            denied = permission and "-permissive" not in command
            return subprocess.CompletedProcess(command, 1 if denied else 0, "",
                f'function "caller" accesses a global mutable binding without {permission}'
                if denied else "")

        self.assertTrue(all(row["passed"] for row in self.run_controls(respond)))

    def test_wrong_product_hash_stops_before_compiler(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            product, report = root / "compiler", root / "report.json"
            product.write_bytes(b"candidate")
            with mock.patch.object(gate, "qualify") as run:
                status = gate.main(["--compiler", str(product), "--report", str(report),
                    "--expected-product-sha256", "0" * 64])
                run.assert_not_called()
            self.assertEqual(status, 1)
            data = json.loads(report.read_text())
            self.assertFalse(data["passed"])
            self.assertIn("expected hash", data["input_error"])

    def test_product_change_invalidates_otherwise_passing_controls(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            product, report = root / "compiler", root / "report.json"
            product.write_bytes(b"candidate")
            before = hashlib.sha256(product.read_bytes()).hexdigest()

            def run(*args, **kwargs):
                product.write_bytes(b"replacement")
                return [{"passed": True}]

            with mock.patch.object(gate, "qualify", side_effect=run):
                status = gate.main(["--compiler", "launcher", "--product", str(product),
                    "--report", str(report), "--expected-product-sha256", before])
            self.assertEqual(status, 1)
            data = json.loads(report.read_text())
            self.assertFalse(data["passed"])
            self.assertIn("changed", data["input_error"])

    def test_missing_compiler_fails(self):
        rows = self.run_controls(mock.Mock(side_effect=FileNotFoundError("missing candidate")))
        self.assertTrue(all(not row["passed"] and row["status"] is None for row in rows))


if __name__ == "__main__":
    unittest.main()
