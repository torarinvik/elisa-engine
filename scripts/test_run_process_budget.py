"""Small subprocess controls for the process-group resource watchdog."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import run_process_budget as budget


class ProcessBudgetTests(unittest.TestCase):
    def run_child(self, code: str, rss_limit_kib: int = 262144, timeout: float = 5.0):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        report_path = root / "report.json"
        log_path = root / "command.log"
        report, status = budget.run_budget([sys.executable, "-c", code], rss_limit_kib,
            timeout, 0.05, report_path, log_path)
        self.assertEqual(json.loads(report_path.read_text()), report)
        return report, status, log_path.read_text()

    def test_completed_child_records_exit_and_log(self):
        report, status, output = self.run_child("print('complete')")
        self.assertEqual(status, 0)
        self.assertTrue(report["passed"])
        self.assertIsNone(report["termination_reason"])
        self.assertIn("complete", output)

    def test_rss_limit_terminates_process_group(self):
        report, status, _ = self.run_child("import time; time.sleep(30)", rss_limit_kib=1)
        self.assertEqual(status, 125)
        self.assertEqual(report["termination_reason"], "rss_limit")
        self.assertGreater(report["peak_aggregate_rss_kib"], 1)
        self.assertTrue(report["passed"] is False)

    def test_timeout_terminates_process_group(self):
        report, status, _ = self.run_child("import time; time.sleep(30)", timeout=0.2)
        self.assertEqual(status, 124)
        self.assertEqual(report["termination_reason"], "timeout")
        pids, _ = budget.process_group_snapshot(report["process_group"])
        self.assertEqual(pids, [])

    def test_timeout_terminates_spawned_descendant(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        child_pid = root / "child.pid"
        code = ("import subprocess,sys,time; "
            "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)']); "
            f"open({str(child_pid)!r},'w').write(str(child.pid)); time.sleep(30)")
        report, status = budget.run_budget([sys.executable, "-c", code], 262144,
            1.0, 0.05, root / "report.json")
        self.assertEqual(status, 124)
        self.assertTrue(child_pid.is_file())
        self.assertEqual(budget.process_group_snapshot(report["process_group"])[0], [])

    def test_rejects_zero_budgets_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                budget.run_budget([sys.executable, "-c", "pass"], 0, 5, 0.1,
                    Path(temporary) / "report.json")

    def test_monitor_failure_stops_the_child(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.object(budget, "process_group_snapshot",
                    side_effect=budget.BudgetMonitorError("ps unavailable")):
                report, status = budget.run_budget([sys.executable, "-c",
                    "import time; time.sleep(30)"], 262144, 5, 0.05,
                    Path(temporary) / "report.json")
        self.assertEqual(status, 125)
        self.assertEqual(report["termination_reason"], "monitor_error")
        self.assertEqual(budget.process_group_snapshot(report["process_group"])[0], [])


if __name__ == "__main__":
    unittest.main()
