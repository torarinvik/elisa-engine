"""Focused tests for strict hosted toolchain provisioning behavior."""

from __future__ import annotations

import contextlib
import io
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
                         "b11e9121c64d94bbc8881db58ae850bf4933ceb9")
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

    def test_lock_rejects_malformed_json_field_types(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lock.json"
            mutations = (
                (lambda lock: [], "root must be an object"),
                (lambda lock: lock["repositories"].__setitem__("elisa_core", []),
                 "repository pin must be an object"),
                (lambda lock: lock["repositories"]["elisa_core"].__setitem__("revision", []),
                 "full lowercase 40-character"),
                (lambda lock: lock["repositories"]["elisa_core"].__setitem__("url", []),
                 "explicit HTTPS"),
                (lambda lock: lock.__setitem__("build_eligibility", []),
                 "build_eligibility"),
                (lambda lock: lock["build_blocker"].__setitem__("kind", []),
                 "build_blocker.kind"),
                (lambda lock: lock["build_blocker"].__setitem__("detail", {}),
                 "build_blocker.kind"),
                (lambda lock: lock.__setitem__("build_eligibility", "ready"),
                 "ready lock must not include build_blocker"),
            )
            for mutate, message in mutations:
                lock = json.loads(json.dumps(self.lock))
                malformed = mutate(lock)
                if malformed is not None:
                    lock = malformed
                path.write_text(json.dumps(lock), encoding="utf-8")
                with self.subTest(message=message), self.assertRaisesRegex(
                        provision.ProvisionError, message):
                    provision.load_lock(path)

    def test_plan_is_read_only_and_reports_pinned_commands(self) -> None:
        result = subprocess.run([sys.executable, str(Path(provision.__file__)), "--plan"],
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["repositories"]["elisascript"]["revision"],
                         "a26f9fd09d59fe1498622feebee3d7b355123b84")
        self.assertEqual(plan["build_eligibility"], "blocked_qualification")
        self.assertEqual(plan["hosted_build_execution"], "deferred")
        self.assertIn("Global.Read/Write", plan["message"])
        self.assertEqual(plan["build_blocker"]["kind"], "proof_global_grants_not_qualified")
        self.assertIn("--seed", plan["build_contract"]["stage1"])
        self.assertEqual(plan["build_contract"]["proof"], ["bash", "scripts/build.sh"])

    def test_blocked_build_fails_before_running_any_toolchain_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            runner = provision.Provisioner(Path(temporary), self.lock)
            with patch.object(runner, "fetch_sources", side_effect=AssertionError("must fail before fetch")):
                with self.assertRaisesRegex(provision.ProvisionError, "Global.Read/Write"):
                    runner.build()
            report = json.loads(runner.report_path.read_text(encoding="utf-8"))
            self.assertEqual(report["state"], "blocked")
            self.assertEqual(report["build_blocker"]["kind"], "proof_global_grants_not_qualified")
            self.assertEqual(report["stages"], [])

    def test_unpublished_compiler_blocker_requires_an_exact_commit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "lock.json"
            lock = json.loads(json.dumps(self.lock))
            lock["build_blocker"] = {"kind": "compiler_revision_not_published", "detail": "blocked"}
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(provision.ProvisionError, "exact required compiler commit"):
                provision.load_lock(path)

    def test_go_preflight_accepts_only_the_exact_locked_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runner = provision.Provisioner(root, self.lock)
            good = subprocess.CompletedProcess(["go", "version"], 0, "go version go1.25.0 darwin/arm64\n", "")
            with patch.object(provision.shutil, "which", return_value="/fake/go"), \
                    patch.object(provision.subprocess, "run", return_value=good):
                self.assertEqual(runner.verify_bootstrap_go(), "1.25.0")
            self.assertEqual(runner.report["toolchain_preflight"]["go"]["state"], "passed")

            bad = subprocess.CompletedProcess(["go", "version"], 0, "go version go1.26.0 darwin/arm64\n", "")
            with patch.object(provision.shutil, "which", return_value="/fake/go"), \
                    patch.object(provision.subprocess, "run", return_value=bad):
                with self.assertRaisesRegex(provision.ProvisionError, "requires 1.25.0, found 1.26.0"):
                    runner.verify_bootstrap_go()
            self.assertEqual(runner.report["toolchain_preflight"]["go"]["state"], "failed")

    def test_build_checks_go_before_any_source_fetch_and_writes_terminal_failure_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock = json.loads(json.dumps(self.lock))
            lock["build_eligibility"] = "ready"
            lock.pop("build_blocker")
            lock_path = root / "ready-lock.json"
            lock_path.write_text(json.dumps(lock), encoding="utf-8")
            workspace = root / "workspace"
            mismatch = subprocess.CompletedProcess(["go", "version"], 0, "go version go1.24.0 darwin/arm64\n", "")
            with patch.object(provision.shutil, "which", return_value="/fake/go"), \
                    patch.object(provision.subprocess, "run", return_value=mismatch), \
                    patch.object(provision.Provisioner, "fetch_sources", side_effect=AssertionError("fetch must not run")), \
                    contextlib.redirect_stderr(io.StringIO()):
                status = provision.main(["--build", "--lock", str(lock_path), "--root", str(workspace)])
            self.assertEqual(status, 1)
            manifest = json.loads((workspace / "toolchain-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["state"], "failed")
            self.assertEqual(manifest["toolchain_preflight"]["go"]["state"], "failed")
            self.assertEqual(manifest["stages"], [])

    def test_unexpected_post_initialization_failure_is_recorded_as_failed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspace = root / "workspace"
            with patch.object(provision.Provisioner, "fetch_sources", side_effect=OSError("mock disk error")), \
                    contextlib.redirect_stderr(io.StringIO()):
                status = provision.main(["--fetch", "--root", str(workspace)])
            self.assertEqual(status, 1)
            manifest = json.loads((workspace / "toolchain-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["state"], "failed")
            self.assertEqual(manifest["failure"]["message"], "mock disk error")

    def test_plan_does_not_create_or_modify_a_workspace(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "must-remain-absent"
            result = subprocess.run([sys.executable, str(Path(provision.__file__)), "--plan", "--root", str(root)],
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(root.exists())

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
