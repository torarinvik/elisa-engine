#!/usr/bin/env python3
"""Run a command in its own process group with time and aggregate-RSS limits."""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
from pathlib import Path


class BudgetMonitorError(RuntimeError):
    pass


def process_group_snapshot(group_id: int) -> tuple[list[int], int]:
    result = subprocess.run(["ps", "-axo", "pid=,pgid=,rss=,stat="], capture_output=True,
        text=True, check=False)
    if result.returncode:
        raise BudgetMonitorError(f"ps exited {result.returncode}: {result.stderr.strip()}")
    pids: list[int] = []
    rss_kib = 0
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 4:
            continue
        try:
            pid, pgid, rss = (int(value) for value in fields[:3])
        except ValueError as error:
            raise BudgetMonitorError(f"invalid ps row: {line!r}") from error
        if pgid == group_id and not fields[3].upper().startswith("Z"):
            pids.append(pid)
            rss_kib += rss
    return pids, rss_kib


def signal_group(group_id: int, signal_number: int) -> None:
    try:
        os.killpg(group_id, signal_number)
    except ProcessLookupError:
        pass


def signal_processes(pids: list[int], signal_number: int) -> None:
    for pid in pids:
        try:
            os.kill(pid, signal_number)
        except ProcessLookupError:
            pass


def terminate_group(group_id: int, process: subprocess.Popen[bytes], grace_seconds: float = 2.0) -> bool:
    try:
        signal_group(group_id, signal.SIGTERM)
    except PermissionError:
        pids, _ = process_group_snapshot(group_id)
        signal_processes(pids, signal.SIGTERM)
    deadline = time.monotonic() + grace_seconds
    while time.monotonic() < deadline:
        # poll() reaps the direct child so its zombie does not appear to keep
        # the process group alive after SIGTERM.
        process.poll()
        try:
            pids, _ = process_group_snapshot(group_id)
        except (BudgetMonitorError, OSError):
            pids = [group_id] if process.poll() is None else []
        if not pids:
            break
        time.sleep(0.05)
    try:
        remaining, _ = process_group_snapshot(group_id)
    except (BudgetMonitorError, OSError):
        remaining = [group_id] if process.poll() is None else []
    forced = bool(remaining)
    if forced:
        try:
            signal_group(group_id, signal.SIGKILL)
        except PermissionError:
            signal_processes(remaining, signal.SIGKILL)
    try:
        process.wait(timeout=grace_seconds)
    except subprocess.TimeoutExpired:
        try:
            signal_group(group_id, signal.SIGKILL)
        except PermissionError:
            try:
                pids, _ = process_group_snapshot(group_id)
            except (BudgetMonitorError, OSError):
                pids = [group_id] if process.poll() is None else []
            signal_processes(pids, signal.SIGKILL)
        process.wait()
        forced = True
    return forced


def write_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def run_budget(command: list[str], rss_limit_kib: int, timeout_seconds: float,
        poll_seconds: float, report_path: Path, log_path: Path | None = None) -> tuple[dict, int]:
    if os.name != "posix":
        raise ValueError("process-group budgets require a POSIX host")
    if not command or rss_limit_kib <= 0 or timeout_seconds <= 0 or poll_seconds <= 0:
        raise ValueError("command and all process budgets must be positive")
    if poll_seconds > 5:
        raise ValueError("poll interval must not exceed five seconds")
    if log_path is not None:
        log_path.parent.mkdir(parents=True, exist_ok=True)

    started = time.monotonic()
    started_unix = time.time()
    process: subprocess.Popen[bytes] | None = None
    peak_rss_kib = 0
    termination_reason: str | None = None
    monitor_error = ""
    forced_kill = False
    status: int | None = None
    output = log_path.open("wb") if log_path is not None else None
    try:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=output,
            stderr=subprocess.STDOUT if output else None, start_new_session=True)
        group_id = process.pid
        deadline = started + timeout_seconds
        while True:
            try:
                pids, current_rss_kib = process_group_snapshot(group_id)
            except BudgetMonitorError as error:
                termination_reason = "monitor_error"
                monitor_error = str(error)
                break
            peak_rss_kib = max(peak_rss_kib, current_rss_kib)
            status = process.poll()
            now = time.monotonic()
            if current_rss_kib > rss_limit_kib:
                termination_reason = "rss_limit"
                break
            if now >= deadline and (status is None or pids):
                termination_reason = "timeout"
                break
            if status is not None and not pids:
                break
            time.sleep(min(poll_seconds, max(0.0, deadline - now)))

        if termination_reason is not None:
            forced_kill = terminate_group(group_id, process)
        status = process.wait()
    except OSError as error:
        monitor_error = str(error)
        termination_reason = "launch_error" if process is None else "monitor_error"
        if process is not None:
            try:
                forced_kill = terminate_group(process.pid, process)
            except OSError as cleanup_error:
                monitor_error += f"; cleanup failed: {cleanup_error}"
                try:
                    process.kill()
                except ProcessLookupError:
                    pass
                status = process.wait()
    finally:
        if output is not None:
            output.close()

    elapsed = time.monotonic() - started
    report = {
        "schema": "elisa-process-budget-report-v1",
        "command": command,
        "process_group": process.pid if process is not None else None,
        "rss_limit_kib": rss_limit_kib,
        "peak_aggregate_rss_kib": peak_rss_kib,
        "timeout_seconds": timeout_seconds,
        "elapsed_seconds": round(elapsed, 3),
        "poll_seconds": poll_seconds,
        "log": str(log_path) if log_path is not None else None,
        "exit_status": status,
        "termination_reason": termination_reason,
        "forced_kill": forced_kill,
        "monitor_error": monitor_error,
        "started_unix": started_unix,
        "finished_unix": time.time(),
        "passed": termination_reason is None and status == 0,
    }
    write_report(report_path, report)
    if termination_reason == "timeout":
        return report, 124
    if termination_reason is not None:
        return report, 125
    return report, status if status is not None else 125


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rss-kib", type=int, required=True, help="aggregate process-group RSS ceiling")
    parser.add_argument("--timeout-seconds", type=float, required=True, help="wall-clock limit")
    parser.add_argument("--poll-seconds", type=float, default=0.25, help="resource sampling interval")
    parser.add_argument("--report", type=Path, required=True, help="JSON report path")
    parser.add_argument("--log", type=Path, help="combined command output path")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="command to run after --")
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command and args.command[0] == "--" else args.command
    try:
        report, status = run_budget(command, args.rss_kib, args.timeout_seconds,
            args.poll_seconds, args.report, args.log)
    except (OSError, ValueError) as error:
        print(f"process budget: {error}", file=sys.stderr)
        return 2
    reason = report["termination_reason"] or "completed"
    print(f"process budget: {reason}; peak aggregate RSS {report['peak_aggregate_rss_kib']} KiB; "
        f"elapsed {report['elapsed_seconds']}s; exit {status}")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
