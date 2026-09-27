#!/usr/bin/env python3
"""Tests for the hosted-CI stage recorder."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "ci_stage.py"


def run_stage(report_dir: Path, job: str, stage: str, *command: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(SCRIPT), str(report_dir), job, stage, "--", *command],
        capture_output=True, text=True, check=False)


class CiStageTests(unittest.TestCase):
    def test_records_pass_and_failure_with_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            reports = Path(temporary)
            passed = run_stage(reports, "cook", "syntax", sys.executable, "-c", "print('stage ok')")
            self.assertEqual(passed.returncode, 0)
            self.assertIn("stage ok", passed.stdout)
            failed = run_stage(reports, "cook", "policy", sys.executable, "-c", "import sys; print('bad'); sys.exit(7)")
            self.assertEqual(failed.returncode, 7)
            report = json.loads((reports / "cook.json").read_text(encoding="utf-8"))
            self.assertEqual(report["evidence_class"], "hosted-portable")
            self.assertEqual(report["hardware_verification"], "unverified")
            self.assertEqual(report["outcome"], "fail")
            self.assertEqual([(entry["name"], entry["state"], entry["status"]) for entry in report["stages"]],
                [("syntax", "pass", 0), ("policy", "fail", 7)])
            self.assertIn("bad", (reports / report["stages"][1]["log"]).read_text(encoding="utf-8"))

    def test_rerun_replaces_stage_and_missing_tool_fails_visibly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            reports = Path(temporary)
            self.assertEqual(run_stage(reports, "native", "probe", sys.executable, "-c", "raise SystemExit(3)").returncode, 3)
            self.assertEqual(run_stage(reports, "native", "probe", sys.executable, "-c", "pass").returncode, 0)
            missing = run_stage(reports, "native", "tool", str(reports / "no-such-tool"))
            self.assertEqual(missing.returncode, 127)
            self.assertIn("cannot start", missing.stderr)
            report = json.loads((reports / "native.json").read_text(encoding="utf-8"))
            self.assertEqual([(entry["name"], entry["status"]) for entry in report["stages"]],
                [("probe", 0), ("tool", 127)])
            self.assertEqual(report["outcome"], "fail")

    def test_rejects_malformed_invocation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            reports = Path(temporary)
            self.assertEqual(run_stage(reports, "../escape", "x", sys.executable).returncode, 2)
            usage = subprocess.run([sys.executable, str(SCRIPT), str(reports), "job", "stage", sys.executable],
                capture_output=True, text=True, check=False)
            self.assertEqual(usage.returncode, 2)
            self.assertFalse(any(reports.iterdir()))


if __name__ == "__main__":
    unittest.main()
