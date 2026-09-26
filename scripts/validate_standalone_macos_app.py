#!/usr/bin/env python3
"""Run a finite packaged app with source projects and Homebrew inaccessible."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile

ENGINE_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True, help="app with a finite validation entry point")
    parser.add_argument("--runs", type=int, default=2, help="independent launches (default: 2)")
    parser.add_argument("--timeout", type=float, default=120, help="maximum seconds per launch")
    args = parser.parse_args()
    if sys.platform != "darwin" or not 1 <= args.runs <= 20 or not 0 < args.timeout <= 600:
        parser.error("requires macOS, 1–20 runs, and a timeout of at most 600 seconds")
    source = args.app.expanduser().resolve()
    try:
        with (source / "Contents/Info.plist").open("rb") as stream:
            executable = plistlib.load(stream)["CFBundleExecutable"]
        if (not isinstance(executable, str) or executable in ("", ".", "..")
                or Path(executable).name != executable):
            raise ValueError("invalid CFBundleExecutable")
        with tempfile.TemporaryDirectory(prefix="Elisa standalone validation ") as folder:
            base = Path(folder).resolve()
            app = base / source.name
            shutil.copytree(source, app)
            profile = base / "deny-build-machine.sb"
            def quote(path: Path) -> str:
                return str(path).replace("\\", "\\\\").replace('"', '\\"')
            profile.write_text("(version 1)\n(allow default)\n(deny network-outbound)\n" + "".join(
                f'(deny file-read* file-write* (subpath "{quote(path)}"))\n'
                for path in (ENGINE_ROOT.parent, Path("/opt/homebrew"))))
            # Prove the profile denies real build-machine files before trusting
            # a successful launch as relocation evidence.
            for control in (ENGINE_ROOT / "IMPLEMENTATION_PLAN.md", Path("/opt/homebrew/bin/brew")):
                if not control.is_file():
                    raise ValueError(f"sandbox control file is missing: {control}")
                result = subprocess.run(["/usr/bin/sandbox-exec", "-f", str(profile),
                    "/bin/cat", str(control)], capture_output=True, timeout=10)
                if result.returncode == 0:
                    raise ValueError(f"sandbox unexpectedly allowed reading {control}")
            environment = {key: value for key, value in os.environ.items()
                if not key.startswith(("ELISA_", "WICKED_", "DYLD_"))}
            launcher = app / "Contents/MacOS" / executable
            for index in range(args.runs):
                result = subprocess.run(["/usr/bin/sandbox-exec", "-f", str(profile), str(launcher)],
                    cwd=base, env=environment, capture_output=True, text=True, timeout=args.timeout)
                if result.returncode != 0:
                    print("\n".join((result.stdout + result.stderr).splitlines()[-60:]), file=sys.stderr)
                    raise ValueError(f"relocated launch {index + 1} exited with {result.returncode}")
                print(f"Relocated launch {index + 1}/{args.runs} passed with source and Homebrew denied", flush=True)
    except (OSError, ValueError, KeyError, plistlib.InvalidFileException, subprocess.TimeoutExpired) as error:
        print(f"standalone app validation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
