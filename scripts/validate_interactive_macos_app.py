#!/usr/bin/env python3
"""Launch a packaged interactive Elisa app with build-machine paths denied."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ENGINE_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MARKERS = ("Created GraphicsDevice_Metal", "[wi::initializer] Wicked Engine Initialized")


def bundle_executable(app: Path) -> str:
    with (app / "Contents/Info.plist").open("rb") as stream:
        value = plistlib.load(stream).get("CFBundleExecutable")
    if (not isinstance(value, str) or value in ("", ".", "..")
            or Path(value).name != value):
        raise ValueError("invalid CFBundleExecutable")
    launcher = app / "Contents/MacOS" / value
    if not launcher.is_file():
        raise ValueError(f"bundle launcher is missing: {launcher}")
    return value


def read_log(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def process_group_exists(process_group: int) -> bool:
    try:
        os.killpg(process_group, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def wait_for_process_group(process_group: int, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while process_group_exists(process_group):
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.05)
    return True


def stop_process_group(process: subprocess.Popen[bytes], force: bool = False) -> int | None:
    process_group = process.pid
    if process.poll() is not None:
        return_code = process.returncode
    else:
        try:
            os.killpg(process_group, signal.SIGKILL if force else signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            return_code = process.wait(timeout=5 if force else 10)
        except subprocess.TimeoutExpired:
            if not force:
                return stop_process_group(process, force=True)
            return_code = process.poll()

    if not wait_for_process_group(process_group, 5 if force else 10):
        try:
            os.killpg(process_group, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if not wait_for_process_group(process_group, 5):
            raise subprocess.TimeoutExpired(str(process_group), 5)
    return return_code


def run_validation(app_source: Path, log_copy: Path,
    timeout: float, stop_after: float, resource_groups: list[str], markers: list[str]) -> dict[str, object]:
    engine = ENGINE_ROOT.resolve()
    app_source = app_source.expanduser().resolve()
    executable_name = bundle_executable(app_source)
    with (app_source / "Contents/Info.plist").open("rb") as stream:
        bundle_id = plistlib.load(stream).get("CFBundleIdentifier")
    if not isinstance(bundle_id, str) or re.fullmatch(r"[A-Za-z0-9.-]{1,255}", bundle_id) is None:
        raise ValueError("invalid CFBundleIdentifier")
    deny_paths = [engine.parent, Path("/opt/homebrew")]
    controls = [engine / "IMPLEMENTATION_PLAN.md", Path("/opt/homebrew/bin/brew")]
    for control in controls:
        if not control.is_file():
            raise ValueError(f"sandbox control file is missing: {control}")

    record: dict[str, object] = {
        "schema": 1,
        "kind": "interactive-package-startup",
        "outcome": "fail",
        "app": str(app_source),
        "bundle_identifier": bundle_id,
        "host": platform.platform(),
        "denied_paths": [str(path) for path in deny_paths],
        "manual_input": "unverified",
        "graceful_shutdown": "unverified",
    }
    process: subprocess.Popen[bytes] | None = None
    latest_log: Path | None = None
    startup_log = ""
    try:
        with tempfile.TemporaryDirectory(prefix="Elisa relocated interactive ") as temporary:
            base = Path(temporary).resolve()
            app = base / f"{app_source.stem} Relocated.app"
            shutil.copytree(app_source, app)
            home = base / "home"
            home.mkdir()
            user_data = base / "user-data"
            user_data.mkdir()
            profile = base / "deny-build-machine.sb"
            quote = lambda path: str(path).replace("\\", "\\\\").replace('"', '\\"')
            profile.write_text("(version 1)\n(allow default)\n(deny network-outbound)\n" + "".join(
                f'(deny file-read* file-write* (subpath "{quote(path)}"))\n' for path in deny_paths),
                encoding="utf-8")
            denied_controls: list[str] = []
            for control in controls:
                result = subprocess.run(["/usr/bin/sandbox-exec", "-f", str(profile),
                    "/bin/cat", str(control)], capture_output=True, timeout=10)
                if result.returncode == 0:
                    raise ValueError(f"sandbox unexpectedly allowed reading {control}")
                denied_controls.append(str(control))

            resources = app / "Contents/Resources"
            resource_counts: dict[str, int] = {}
            for group in resource_groups:
                relative = PurePosixPath(group)
                if relative.is_absolute() or ".." in relative.parts or not relative.parts:
                    raise ValueError(f"invalid resource group: {group}")
                directory = resources.joinpath(*relative.parts)
                count = sum(1 for path in directory.rglob("*") if path.is_file()) if directory.is_dir() else 0
                if count == 0:
                    raise ValueError(f"declared resource group is missing or empty: {group}")
                resource_counts[group] = count

            environment = {key: value for key, value in os.environ.items()
                if not key.startswith(("ELISA_", "WICKED_", "DYLD_"))}
            environment.update({"PATH": "/usr/bin:/bin", "HOME": str(home),
                "ELISA_USER_DATA_DIR": str(user_data)})
            launcher = app / "Contents/MacOS" / executable_name
            record["launcher_sha256"] = hashlib.sha256(launcher.read_bytes()).hexdigest()
            process = subprocess.Popen(["/usr/bin/sandbox-exec", "-f", str(profile), str(launcher)],
                cwd=base, env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                start_new_session=True)
            latest_log = home / "Library/Logs/Elisa" / bundle_id / "latest.log"
            expected = [*DEFAULT_MARKERS, *markers]
            expected.append(f"Shader source path: {resources / 'shaders'}/")
            started_at = time.monotonic()
            deadline = started_at + timeout
            while time.monotonic() < deadline:
                startup_log = read_log(latest_log)
                if all(marker in startup_log for marker in expected):
                    break
                if process.poll() is not None:
                    raise ValueError(f"interactive app exited early with status {process.returncode}: "
                        + "\n".join(startup_log.splitlines()[-50:]))
                time.sleep(0.2)
            else:
                raise ValueError("timed out waiting for startup markers: " + repr(expected) + "\n"
                    + "\n".join(startup_log.splitlines()[-50:]))
            if "Elisa shader manifest rejected" in startup_log:
                raise ValueError("packaged shader manifest was rejected")

            record.update({
                "outcome": "pass",
                "relocated_path_contains_spaces": True,
                "network_outbound_denied_by_profile": True,
                "sandbox_controls_denied": denied_controls,
                "resource_file_counts": resource_counts,
                "startup_markers": expected,
                "startup_seconds": round(time.monotonic() - started_at, 3),
                "termination": "SIGTERM after successful startup; graceful shutdown not assessed",
            })
            if stop_after:
                try:
                    process.wait(timeout=stop_after)
                except subprocess.TimeoutExpired:
                    pass
            if process.poll() is None:
                record["launcher_returncode_after_termination"] = stop_process_group(process)
            else:
                if process.returncode != 0:
                    raise ValueError(f"interactive app exited with status {process.returncode}")
                record["graceful_shutdown"] = "verified"
                record["termination"] = "clean exit before stop timeout"
                record["launcher_returncode"] = process.returncode
            process = None
            startup_log = read_log(latest_log)
            if "process_exit_status=" in startup_log:
                record["launcher_logged_exit_status"] = startup_log.rsplit("process_exit_status=", 1)[1].splitlines()[0]
            log_copy.parent.mkdir(parents=True, exist_ok=True)
            log_copy.write_text(startup_log, encoding="utf-8")
            record["launcher_log"] = str(log_copy.resolve())
    finally:
        if process is not None:
            stop_process_group(process, force=True)
            startup_log = read_log(latest_log) if latest_log is not None else startup_log
            if startup_log:
                log_copy.parent.mkdir(parents=True, exist_ok=True)
                log_copy.write_text(startup_log, encoding="utf-8")
                record["launcher_log"] = str(log_copy.resolve())
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True, help="interactive .app bundle")
    parser.add_argument("--report", type=Path,
        default=ENGINE_ROOT / "build/validation/interactive-app-startup.json")
    parser.add_argument("--log", type=Path,
        default=ENGINE_ROOT / "build/validation/interactive-app-startup.log")
    parser.add_argument("--timeout", type=float, default=60, help="startup timeout in seconds")
    parser.add_argument("--stop-after", type=float, default=2,
        help="seconds to leave the app running after startup markers pass")
    parser.add_argument("--resource-group", action="append", default=[],
        help="required non-empty directory under Contents/Resources (repeatable)")
    parser.add_argument("--marker", action="append", default=[],
        help="additional required startup log text (repeatable)")
    args = parser.parse_args()
    if sys.platform != "darwin" or not (0 < args.timeout <= 600) or not (0 <= args.stop_after <= 600):
        parser.error("requires macOS, a positive timeout up to 600, and a stop delay from 0 to 600 seconds")
    report = args.report.expanduser().resolve()
    log = args.log.expanduser().resolve()
    report.parent.mkdir(parents=True, exist_ok=True)
    log.parent.mkdir(parents=True, exist_ok=True)
    try:
        log.unlink()
    except FileNotFoundError:
        pass
    record: dict[str, object]
    try:
        record = run_validation(args.app, log, args.timeout, args.stop_after,
            args.resource_group, args.marker)
    except (OSError, ValueError, KeyError, plistlib.InvalidFileException,
            subprocess.SubprocessError) as error:
        record = {"schema": 1, "kind": "interactive-package-startup", "outcome": "fail",
            "app": str(args.app.expanduser()), "error": str(error),
            "manual_input": "unverified", "graceful_shutdown": "unverified"}
        if log.is_file():
            record["launcher_log"] = str(log)
    report.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(record, indent=2, sort_keys=True))
    return 0 if record.get("outcome") == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
