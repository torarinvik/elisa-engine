#!/usr/bin/env python3
"""Provision the pinned Elisa toolchain for hosted, headless macOS CI.

Only source checkouts and build products below --root are touched. Each stage
has a durable JSON record and stdout/stderr log, including when the stage fails.
The source refs must be full immutable commit IDs from the checked-in lock file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ENGINE_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOCK = ENGINE_ROOT / "scripts/hosted_toolchain.lock.json"
REPOSITORIES = ("elisa_core", "elisa_compiler", "elisa_proof", "elisascript")


class ProvisionError(RuntimeError):
    pass


def load_lock(path: Path) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProvisionError(f"cannot read toolchain lock {path}: {error}") from error
    if not isinstance(lock, dict):
        raise ProvisionError("hosted toolchain lock root must be an object")
    if lock.get("schema") != "elisa-engine-hosted-toolchain-lock-v1":
        raise ProvisionError("unsupported hosted toolchain lock schema")
    if not isinstance(lock.get("bootstrap_go"), str) or not lock["bootstrap_go"]:
        raise ProvisionError("lock must pin bootstrap_go")
    repositories = lock.get("repositories")
    if not isinstance(repositories, dict) or set(repositories) != set(REPOSITORIES):
        raise ProvisionError(f"lock repositories must be exactly {', '.join(REPOSITORIES)}")
    for name, spec in repositories.items():
        if not isinstance(spec, dict):
            raise ProvisionError(f"{name} repository pin must be an object")
        revision = spec.get("revision")
        if not isinstance(revision, str) or len(revision) != 40 or any(
                char not in "0123456789abcdef" for char in revision):
            raise ProvisionError(f"{name} revision must be a full lowercase 40-character commit SHA")
        url = spec.get("url")
        if not isinstance(url, str) or not url.startswith("https://github.com/") or not url.endswith(".git"):
            raise ProvisionError(f"{name} must use an explicit HTTPS GitHub repository URL")
    upstream = lock.get("upstream_ref_heads")
    if not isinstance(upstream, dict) or upstream.get("ref") != "refs/heads/main":
        raise ProvisionError("lock upstream_ref_heads must record refs/heads/main")
    if not isinstance(upstream.get("checked_at"), str) or not upstream["checked_at"].strip():
        raise ProvisionError("lock upstream_ref_heads must include checked_at")
    heads = upstream.get("heads")
    if not isinstance(heads, dict) or set(heads) != set(REPOSITORIES):
        raise ProvisionError(f"upstream main heads must be exactly {', '.join(REPOSITORIES)}")
    for name, revision in heads.items():
        if not isinstance(revision, str) or len(revision) != 40 or any(
                char not in "0123456789abcdef" for char in revision):
            raise ProvisionError(f"{name} upstream head must be a full lowercase 40-character commit SHA")
    if heads["elisa_compiler"] != repositories["elisa_compiler"]["revision"]:
        raise ProvisionError("compiler pin must match the recorded upstream main head")
    contract = lock.get("build_contract")
    if not isinstance(contract, dict) or set(contract) != {"stage0", "stage1", "proof", "elisascript"}:
        raise ProvisionError("lock build_contract must define stage0, stage1, proof, and elisascript")
    if any(not isinstance(command, list) or not command or any(not isinstance(arg, str) for arg in command)
           for command in contract.values()):
        raise ProvisionError("each build_contract command must be a nonempty array of strings")
    eligibility = lock.get("build_eligibility")
    if not isinstance(eligibility, str) or eligibility not in {"ready", "blocked_qualification"}:
        raise ProvisionError("lock build_eligibility must be ready or blocked_qualification")
    blocker = lock.get("build_blocker")
    if eligibility == "blocked_qualification":
        if not isinstance(blocker, dict) or any(
                not isinstance(blocker.get(key), str) or not blocker[key].strip()
                for key in ("kind", "detail")):
            raise ProvisionError("blocked lock must include build_blocker.kind and actionable detail")
        if blocker["kind"] == "compiler_revision_not_published":
            required = blocker.get("required_compiler_commit")
            if not isinstance(required, str) or len(required) != 40 or any(
                    char not in "0123456789abcdef" for char in required):
                raise ProvisionError("build blocker must name the exact required compiler commit")
    elif blocker is not None:
        raise ProvisionError("ready lock must not include build_blocker")
    return lock


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class Provisioner:
    def __init__(self, root: Path, lock: dict[str, Any]):
        self.root = root.resolve()
        self.lock = lock
        self.sources = self.root / "sources"
        self.logs = self.root / "logs"
        self.output = self.root / "bin"
        self.report_path = self.root / "toolchain-manifest.json"
        self.report: dict[str, Any] = {
            "schema": "elisa-engine-hosted-toolchain-report-v1",
            "state": "in_progress",
            "host": {"system": platform.system(), "machine": platform.machine()},
            "workspace_root": os.path.relpath(self.root, ENGINE_ROOT),
            "lock": lock,
            "stages": [],
            "artifacts": {},
        }
        self.root.mkdir(parents=True, exist_ok=True)
        self.logs.mkdir(parents=True, exist_ok=True)
        self.output.mkdir(parents=True, exist_ok=True)

    def save(self) -> None:
        temporary = self.report_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(self.report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(self.report_path)

    def display_path(self, path: Path) -> str:
        try:
            return path.resolve().relative_to(self.root).as_posix()
        except ValueError:
            return str(path)

    def stage(self, name: str, argv: list[str], cwd: Path, env: dict[str, str]) -> None:
        log_path = self.logs / f"{name}.log"
        entry: dict[str, Any] = {
            "name": name,
            "argv": argv,
            "cwd": self.display_path(cwd),
            "started_unix": int(time.time()),
            "log": log_path.relative_to(self.root).as_posix(),
        }
        self.report["stages"].append(entry)
        self.save()
        try:
            with log_path.open("wb") as log:
                result = subprocess.run(argv, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, check=False)
            entry["exit_status"] = result.returncode
            entry["finished_unix"] = int(time.time())
            if result.returncode:
                entry["state"] = "failed"
                self.report["state"] = "failed"
                self.save()
                raise ProvisionError(f"stage {name} failed with status {result.returncode}; see {self.display_path(log_path)}")
            entry["state"] = "passed"
            entry["log_sha256"] = sha256(log_path)
            self.save()
        except OSError as error:
            entry["state"] = "failed"
            entry["error"] = str(error)
            entry["finished_unix"] = int(time.time())
            self.report["state"] = "failed"
            self.save()
            raise ProvisionError(f"cannot run stage {name}: {error}") from error

    def checkout(self, name: str, spec: dict[str, str]) -> Path:
        destination = self.sources / name
        if destination.exists():
            head = self.git_value(destination, "rev-parse", "HEAD")
            if head != spec["revision"]:
                raise ProvisionError(f"existing {name} checkout is {head}, expected {spec['revision']}; use a new --root")
            dirty = self.git_value(destination, "status", "--porcelain", "--untracked-files=all")
            if dirty:
                raise ProvisionError(f"existing {name} checkout has working-tree changes; use a new --root")
            return destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        self.stage(f"clone-{name}", ["git", "clone", "--no-checkout", "--filter=blob:none", spec["url"], str(destination)], self.root, os.environ.copy())
        self.stage(f"fetch-{name}", ["git", "-C", str(destination), "fetch", "--no-tags", "origin", spec["revision"]], self.root, os.environ.copy())
        self.stage(f"checkout-{name}", ["git", "-C", str(destination), "checkout", "--detach", spec["revision"]], self.root, os.environ.copy())
        actual = self.git_value(destination, "rev-parse", "HEAD")
        if actual != spec["revision"]:
            raise ProvisionError(f"{name} checkout resolved to {actual}, expected pinned {spec['revision']}")
        if self.git_value(destination, "status", "--porcelain", "--untracked-files=all"):
            raise ProvisionError(f"fresh {name} checkout is unexpectedly dirty")
        return destination

    @staticmethod
    def git_value(path: Path, *args: str) -> str:
        result = subprocess.run(["git", "-C", str(path), *args], text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise ProvisionError(f"git command failed in {path}: {result.stderr.strip()}")
        return result.stdout.strip()

    def fetch_sources(self) -> dict[str, Path]:
        sources = {name: self.checkout(name, spec) for name, spec in self.lock["repositories"].items()}
        self.report["sources"] = {
            name: {"url": self.lock["repositories"][name]["url"], "revision": self.lock["repositories"][name]["revision"], "path": self.display_path(path)}
            for name, path in sources.items()
        }
        self.save()
        return sources

    def command(self, name: str, **values: str) -> list[str]:
        try:
            return [part.format(**values) for part in self.lock["build_contract"][name]]
        except (KeyError, ValueError) as error:
            raise ProvisionError(f"invalid {name} build command in lock: {error}") from error

    def build(self) -> None:
        if self.lock["build_eligibility"] != "ready":
            self.report["state"] = "blocked"
            self.report["build_blocker"] = self.lock["build_blocker"]
            self.save()
            raise ProvisionError(self.lock["build_blocker"]["detail"])
        self.verify_bootstrap_go()
        sources = self.fetch_sources()
        core, compiler, proof, script = (sources[key] for key in REPOSITORIES)
        llvm_prefix = os.environ.get("LLVM_PREFIX", "")
        if not llvm_prefix:
            brew = shutil.which("brew")
            if not brew:
                raise ProvisionError("Homebrew is required to resolve the pinned LLVM build prerequisites")
            result = subprocess.run([brew, "--prefix", "llvm"], text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, check=False)
            if result.returncode:
                raise ProvisionError(f"brew --prefix llvm failed: {result.stderr.strip()}")
            llvm_prefix = result.stdout.strip()
            if not llvm_prefix:
                raise ProvisionError("brew --prefix llvm returned an empty path")
        llvm_config = os.environ.get("LLVM_CONFIG", str(Path(llvm_prefix) / "bin/llvm-config"))
        llvm_clang = os.environ.get("ELISA_CLANG", str(Path(llvm_prefix) / "bin/clang"))
        base_env = os.environ.copy()
        base_env.update({"ELISA_CORE": str(core), "ELISACORE_BIN": str(core / "compiler/bin/elisac"),
                         "LLVM_CONFIG": llvm_config, "ELISA_CLANG": llvm_clang,
                         "ELISA_STAGE1_BIN": str(compiler / "bin/elisac-stage1"),
                         "ELISA_STAGE1_GLOBAL_SEED_LOCK_DIR": str(self.root / "locks/stage1-seed.lock")})
        self.stage("build-stage0", self.command("stage0", elisa_core=str(core)), core, base_env)
        self.stage("seed-stage1", self.command("stage1"), compiler, base_env)

        proof_env = base_env.copy()
        proof_env.update({
            "PATH": os.pathsep.join((str(compiler / "scripts"), str(compiler / "bin"), str(core / "compiler/bin"), proof_env.get("PATH", ""))),
            "ELISA_COMPILER_BIN": str(compiler / "scripts/elisac_stage1.sh"),
            "ELISA_STAGE1_ROOT": str(compiler),
            "ELISA_COMPILER_SRC": str(compiler),
            "ELISA_COMPILER_REV": self.lock["repositories"]["elisa_compiler"]["revision"],
            "ELISA_RUNTIME_OBJ": str(compiler / "build/runtime/elisacore_runtime.o"),
            "ELISA_PROOF_COMPILE_MODE": "strict",
            "ELISA_PROOF_OUTPUT": str(self.output / "elisa-proof"),
            "ELISA_PROOF_OBJECT_CACHE": str(self.root / "cache/proof-objects"),
        })
        self.stage("build-proof", self.command("proof"), proof, proof_env)

        script_env = base_env.copy()
        script_env.update({"ELISA_STAGE1_ROOT": str(compiler), "ELISA_RUNTIME_OBJ": str(compiler / "build/runtime/elisacore_runtime.o")})
        script_command = self.command("elisascript", compiler=str(compiler), output=str(self.output))
        self.stage("build-elisascript", script_command, script, script_env)
        self.record_artifacts(compiler, proof)
        self.report["state"] = "passed"
        self.report["hardware_verification"] = "unverified"
        self.report["evidence_class"] = "hosted-portable"
        self.save()

    def verify_bootstrap_go(self) -> str:
        executable = shutil.which("go")
        if not executable:
            self.report["toolchain_preflight"] = {
                "go": {"expected": self.lock["bootstrap_go"], "state": "failed", "error": "go is not on PATH"}
            }
            self.save()
            raise ProvisionError("locked Go bootstrap is unavailable: go is not on PATH")
        result = subprocess.run([executable, "version"], text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False)
        output = result.stdout.strip()
        match = re.search(r"(?:^|\s)go version go([^\s]+)", output)
        observed = match.group(1) if match else ""
        go_record = {
            "expected": self.lock["bootstrap_go"],
            "observed": observed,
            "executable": self.display_path(Path(executable)),
            "version_output": output,
            "state": "passed" if result.returncode == 0 and observed == self.lock["bootstrap_go"] else "failed",
        }
        self.report["toolchain_preflight"] = {"go": go_record}
        self.save()
        if result.returncode != 0:
            raise ProvisionError(f"go version failed with status {result.returncode}: {result.stderr.strip()}")
        if observed != self.lock["bootstrap_go"]:
            raise ProvisionError(f"Go version mismatch: lock requires {self.lock['bootstrap_go']}, found {observed or 'unrecognized'}")
        return observed

    def mark_failed(self, error: Exception) -> None:
        if self.report.get("state") in {"blocked", "failed", "passed"}:
            return
        for entry in reversed(self.report.get("stages", [])):
            if "state" not in entry:
                entry["state"] = "failed"
                entry["error"] = str(error)
                entry["finished_unix"] = int(time.time())
                break
        self.report["state"] = "failed"
        self.report["failure"] = {"type": type(error).__name__, "message": str(error)}
        self.save()

    def record_artifacts(self, compiler: Path, proof: Path) -> None:
        products = {
            "stage0_compiler": self.sources / "elisa_core/compiler/bin/elisac",
            "stage1_compiler": compiler / "bin/elisac-stage1",
            "proof": self.output / "elisa-proof",
            "elisascript": self.output / "elisascript",
        }
        for name, path in products.items():
            if not path.is_file():
                raise ProvisionError(f"expected tool product is missing: {path}")
            self.report["artifacts"][name] = {"path": self.display_path(path), "sha256": sha256(path), "size_bytes": path.stat().st_size}
        proof_manifest = proof / "build/elisa-proof.manifest.json"
        if not proof_manifest.is_file():
            raise ProvisionError(f"prover did not emit its provenance manifest: {proof_manifest}")
        self.report["artifacts"]["proof_build_manifest"] = {
            "path": self.display_path(proof_manifest), "sha256": sha256(proof_manifest),
        }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--root", type=Path, default=ENGINE_ROOT / "build/hosted-toolchain")
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--fetch", action="store_true", help="fetch exact source commits only")
    operation.add_argument("--build", action="store_true", help="build all pinned compiler/prover/script products")
    operation.add_argument("--plan", action="store_true", help="print pins and documented build commands without running them")
    args = parser.parse_args(argv)
    provisioner: Provisioner | None = None
    try:
        lock = load_lock(args.lock)
        if args.plan:
            message = ("Hosted toolchain build is enabled for the pinned revisions."
                if lock["build_eligibility"] == "ready" else
                f"Hosted toolchain build is deferred: {lock['build_blocker']['detail']}")
            print(json.dumps({"schema": lock["schema"], "bootstrap_go": lock["bootstrap_go"],
                              "build_eligibility": lock["build_eligibility"], "build_blocker": lock.get("build_blocker"),
                              "hosted_build_execution": "deferred" if lock["build_eligibility"] != "ready" else "enabled",
                              "message": message,
                              "upstream_ref_heads": lock["upstream_ref_heads"],
                              "repositories": lock["repositories"], "build_contract": lock["build_contract"]}, indent=2, sort_keys=True))
            return 0
        provisioner = Provisioner(args.root, lock)
        if args.fetch:
            provisioner.fetch_sources()
            provisioner.report["state"] = "sources_ready"
            provisioner.save()
        else:
            provisioner.build()
        return 0
    except Exception as error:
        if provisioner is not None:
            try:
                provisioner.mark_failed(error)
            except OSError:
                # If the workspace itself has become unwritable, no report
                # writer can make the failure durable; preserve the original error.
                pass
        print(f"toolchain provisioning failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        error = ProvisionError("provisioning interrupted")
        if provisioner is not None:
            try:
                provisioner.mark_failed(error)
            except OSError:
                pass
        print(f"toolchain provisioning failed: {error}", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
