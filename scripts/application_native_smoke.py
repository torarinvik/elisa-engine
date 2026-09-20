#!/usr/bin/env python3
"""Build and run the Elisa-authored application lifecycle smoke project."""

from __future__ import annotations

import json
import math
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="Elisa application smoke ") as temporary_directory:
        project = Path(temporary_directory)
        manifest = {
            "name": "application-native-smoke",
            "main": str(ROOT / "test/application_native_main.elisa"),
            "output": "build/application-native-smoke",
            "application": {
                "title": "Elisa Engine Smoke",
                "width": 320,
                "height": 200,
                "hidden": True,
            },
        }
        (project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
        fixture = project / "test/fixtures/audio-smoke.wav"
        fixture.parent.mkdir(parents=True, exist_ok=True)
        samples = [int(6000 * math.sin(2.0 * math.pi * 440.0 * frame / 8000)) for frame in range(400)]
        with wave.open(str(fixture), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8000)
            output.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))
        command = [sys.executable, str(ROOT / "scripts/elisa_build_run.py"), "run", "--project", str(project)]
        status = subprocess.run(command, check=False).returncode
    if status == 0:
        print("Elisa main application lifecycle passed through the generic build/run command.")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
