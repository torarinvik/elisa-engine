#!/usr/bin/env python3
"""Run one hosted-CI stage and record it in a structured, retained report.

Usage: ci_stage.py REPORT_DIR JOB STAGE -- COMMAND [ARG...]

The command's combined output is streamed to the caller and kept in
REPORT_DIR/logs/JOB-STAGE.log. REPORT_DIR/JOB.json accumulates one entry per
stage with its exit status and log path. Hosted runners have no GPU session or
pinned Elisa toolchain, so every report states `hardware_verification:
unverified` and `evidence_class: hosted-portable`; GPU workstation evidence is
recorded separately by `native_gate.elisascript` in `build/native-gate.json`.
The stage's own exit status is returned so a failing step still fails the job.
"""

import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path

NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")


def load_report(path: Path, job: str) -> dict:
    if path.is_file():
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            report = None
        if isinstance(report, dict) and report.get("job") == job and isinstance(report.get("stages"), list):
            return report
    return {
        "schema": 1,
        "job": job,
        "evidence_class": "hosted-portable",
        "hardware_verification": "unverified",
        "provenance": {
            "git_revision": os.environ.get("GITHUB_SHA", "unknown"),
            "run_id": os.environ.get("GITHUB_RUN_ID", ""),
            "runner_os": os.environ.get("RUNNER_OS", platform.system()),
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "stages": [],
    }


def main(argv: list[str]) -> int:
    if len(argv) < 5 or argv[3] != "--":
        print("usage: ci_stage.py REPORT_DIR JOB STAGE -- COMMAND [ARG...]", file=sys.stderr)
        return 2
    report_dir, job, stage, command = Path(argv[0]), argv[1], argv[2], argv[4:]
    if not NAME.match(job) or not NAME.match(stage):
        print("job and stage names must be 1-64 characters of [A-Za-z0-9_.-]", file=sys.stderr)
        return 2
    logs = report_dir / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log_path = logs / f"{job}-{stage}.log"
    started = time.time()
    with log_path.open("wb") as log:
        try:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        except OSError as error:
            message = f"ci_stage: cannot start {command[0]}: {error}\n".encode()
            log.write(message)
            sys.stderr.buffer.write(message)
            status = 127
        else:
            assert process.stdout is not None
            for chunk in iter(lambda: process.stdout.read(8192), b""):
                log.write(chunk)
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            status = process.wait()
    report_path = report_dir / f"{job}.json"
    report = load_report(report_path, job)
    report["stages"] = [entry for entry in report["stages"] if entry.get("name") != stage]
    report["stages"].append({
        "name": stage,
        "status": status,
        "state": "pass" if status == 0 else "fail",
        "seconds": round(time.time() - started, 3),
        "log": str(log_path.relative_to(report_dir)),
    })
    report["outcome"] = "pass" if all(entry["status"] == 0 for entry in report["stages"]) else "fail"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return status


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
