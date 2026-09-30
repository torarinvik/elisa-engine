#!/usr/bin/env python3
"""Induce a fatal signal in a packaged app and check its local crash report.

Launches the bundle's launcher with a temporary ELISA_CRASH_DIR, lets it run
for a fixed warm-up, sends the signal, and requires that the process died by
that signal and left crash-PID.txt naming the build identity and arguments.
The main image's frames are symbolized with `atos` from the recorded load
address, so the report is usable without the machine that crashed.
"""

from __future__ import annotations

import argparse
import re
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


def symbolize(image: str, load_address: str, frames: list[str]) -> list[str]:
    addresses = []
    for frame in frames:
        match = re.search(r"\b(0x[0-9a-fA-F]+)\b", frame)
        if match:
            addresses.append(match.group(1))
    if not addresses:
        return []
    result = subprocess.run(["atos", "-o", image, "-l", load_address, *addresses],
        capture_output=True, text=True, check=False)
    return result.stdout.splitlines() if result.returncode == 0 else []


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--warmup", type=float, default=6.0)
    arguments = parser.parse_args()
    launcher = next((arguments.app / "Contents/MacOS").iterdir())
    with tempfile.TemporaryDirectory(prefix="elisa crash report ") as temporary:
        reports = Path(temporary)
        environment = {"PATH": "/usr/bin:/bin", "HOME": temporary, "ELISA_CRASH_DIR": str(reports)}
        process = subprocess.Popen([str(launcher), "--induced-crash-probe"], env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            process.wait(timeout=arguments.warmup)
            print(f"App exited with {process.returncode} before the induced crash.", file=sys.stderr)
            return 1
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGSEGV)
        _, stderr = process.communicate(timeout=30)
        if process.returncode != -signal.SIGSEGV:
            print(f"App ended with {process.returncode}, not SIGSEGV.", file=sys.stderr)
            return 2
        found = sorted(reports.glob("crash-*.txt"))
        if len(found) != 1:
            print(f"Expected one crash report, found {len(found)}.", file=sys.stderr)
            return 3
        text = found[0].read_text(encoding="utf-8", errors="replace")
        fields = dict(line.split(": ", 1) for line in text.splitlines() if ": " in line and not line[0].isdigit())
        identity = stderr.splitlines()[0] if stderr else ""
        if not text.startswith("signal: SIGSEGV\n"):
            return 4
        if not identity.startswith("Elisa package: ") or fields.get("build") != identity:
            print("Crash report does not carry the launcher's build identity.", file=sys.stderr)
            return 5
        if "[--induced-crash-probe]" not in fields.get("arguments", ""):
            return 6
        frames = text.split("frames:\n", 1)[1].splitlines() if "frames:\n" in text else []
        if len(frames) < 3:
            return 7
        symbols = symbolize(fields.get("image", ""), fields.get("load_address", ""), frames)
        named = [line for line in symbols if not line.startswith("0x")]
        if not named:
            print("atos could not name any frame.", file=sys.stderr)
            return 8
        print(text)
        print("atos:")
        print("\n".join(symbols[:12]))
    print("Packaged induced crash left a symbolizable local report.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
