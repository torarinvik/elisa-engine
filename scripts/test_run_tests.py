#!/usr/bin/env python3
"""Tests that the gate retains diagnostics for every failed test row."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_tests


class FailureReportTests(unittest.TestCase):
    def test_report_keeps_all_failed_rows_and_omits_passes(self):
        results = {
            0: ({"name": "first", "source": "test/first.elisa", "binary": "build/first",
                 "flags": [], "args": []}, 1, "", "first diagnostic", "compile"),
            1: ({"name": "second", "source": "test/second.elisa", "binary": "build/second",
                 "flags": ["-O0"], "args": ["--probe"]}, 2, "runtime output", "runtime diagnostic", "built"),
            2: ({"name": "passed", "source": "test/passed.elisa", "binary": "build/passed",
                 "flags": [], "args": []}, 0, "", "", "cached"),
        }

        report = run_tests.failure_report("compiler-hash", results)

        self.assertEqual(report["schema"], 1)
        self.assertEqual(report["compiler_entry_sha256"], "compiler-hash")
        self.assertEqual([item["name"] for item in report["failures"]], ["first", "second"])
        self.assertEqual(report["failures"][0]["stderr"], "first diagnostic")
        self.assertEqual(report["failures"][1]["stdout"], "runtime output")
        self.assertEqual(report["failures"][1]["args"], ["--probe"])


if __name__ == "__main__":
    unittest.main()
