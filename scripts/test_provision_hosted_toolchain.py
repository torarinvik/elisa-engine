"""Focused tests for strict hosted toolchain provisioning behavior."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import provision_hosted_toolchain as provision


class HostedToolchainProvisionerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.lock = provision.load_lock(provision.DEFAULT_LOCK)

    def test_lock_pins_full_immutable_commits_for_all_tools(self) -> None:
        self.assertEqual(set(self.lock["repositories"]), set(provision.REPOSITORIES))
        self.assertEqual(self.lock["bootstrap_go"], "1.25.0")
        self.assertEqual(self.lock["repositories"]["elisa_compiler"]["revision"],
                         "72a752820ab581d46bb17b3fb7158ba3879a16e3")
        self.assertEqual(self.lock["repositories"]["elisa_proof"]["revision"],
                         "04601de17703ac3ffa1b128086e8d6ea047e9501")

    def test_lock_rejects_branch_names_and_implicit_urls(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lock.json"
            bad = json.loads(json.dumps(self.lock))
            bad["repositories"]["elisa_core"]["revision"] = "main"
            path.write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaisesRegex(provision.ProvisionError, "full lowercase"):
                provision.load_lock(path)
            bad["repositories"]["elisa_core"]["revision"] = self.lock["repositories"]["elisa_core"]["revision"]
            bad["repositories"]["elisa_core"]["url"] = "Elisa-core"
            path.write_text(json.dumps(bad), encoding="utf-8")
            with self.assertRaisesRegex(provision.ProvisionError, "explicit HTTPS"):
                provision.load_lock(path)

    def test_plan_is_read_only_and_reports_pinned_commands(self) -> None:
        result = subprocess.run([sys.executable, str(Path(provision.__file__)), "--plan"],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["repositories"]["elisascript"]["revision"],
                         "a26f9fd09d59fe1498622feebee3d7b355123b84")
        self.assertEqual(plan["build_eligibility"], "blocked_upstream_compatibility")
        self.assertEqual(plan["build_blocker"]["required_compiler_commit"],
                         "955cde86f336dff0945e8918303e9f974cc97532")
        self.assertIn("--seed", plan["build_contract"]["stage1"])
        self.assertEqual(plan["build_contract"]["proof"], ["bash", "scripts/build.sh"])

    def test_blocked_build_fails_before_running_any_toolchain_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runner = provision.Provisioner(Path(temporary), self.lock)
            with patch.object(runner, "fetch_sources", side_effect=AssertionError("must fail before fetch")):
                with self.assertRaisesRegex(provision.ProvisionError, "not advertised by an upstream ref"):
                    runner.build()
            report = json.loads(runner.report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["state"], "blocked")
            self.assertEqual(report["build_blocker"]["kind"], "compiler_revision_not_published")
            self.assertEqual(report["stages"], [])

    def test_failed_stage_is_retained_with_log_and_stops_the_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runner = provision.Provisioner(root, self.lock)
            command = [sys.executable, "-c", "import sys; print('mock build failure'); sys.exit(9)"]
            with self.assertRaisesRegex(provision.ProvisionError, "status 9"):
                runner.stage("mock-build", command, root, {})
            report = json.loads(runner.report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["state"], "failed")
            self.assertEqual(report["stages"][0]["exit_status"], 9)
            self.assertEqual(report["stages"][0]["state"], "failed")
            self.assertIn("mock build failure", root.joinpath(report["stages"][0]["log"]).read_text(encoding="utf-8"))

    def test_successful_stage_records_log_digest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runner = provision.Provisioner(root, self.lock)
            runner.stage("mock-build", [sys.executable, "-c", "print('ok')"], root, {})
            entry = runner.report["stages"][0]
            self.assertEqual(entry["state"], "passed")
            self.assertEqual(len(entry["log_sha256"]), 64)

    def test_checkout_fetches_exact_commit_detached_and_refuses_a_mismatched_existing_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            working = root / "working"
            bare = root / "remote.git"
            working.mkdir()
            subprocess.run(["git", "init", "-q", str(working)], check=True)
            (working / "source.txt").write_text("pinned\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(working), "add", "source.txt"], check=True)
            subprocess.run(["git", "-C", str(working), "-c", "user.name=CI", "-c", "user.email=ci@example.invalid",
                            "commit", "-qm", "fixture"], check=True)
            revision = subprocess.check_output(["git", "-C", str(working), "rev-parse", "HEAD"], text=True).strip()
            subprocess.run(["git", "clone", "-q", "--bare", str(working), str(bare)], check=True)
            runner = provision.Provisioner(root / "workspace", self.lock)
            checkout = runner.checkout("fixture", {"url": str(bare), "revision": revision})
            self.assertEqual(runner.git_value(checkout, "rev-parse", "HEAD"), revision)
            self.assertNotEqual(subprocess.run(["git", "-C", str(checkout), "symbolic-ref", "-q", "HEAD"],
                                               check=False).returncode, 0)
            (checkout / "unexpected.txt").write_text("do not silently reuse\n", encoding="utf-8")
            with self.assertRaisesRegex(provision.ProvisionError, "working-tree changes"):
                runner.checkout("fixture", {"url": str(bare), "revision": revision})


if __name__ == "__main__":
    unittest.main()
