#!/usr/bin/env python3
"""Build and run the Elisa-authored application lifecycle smoke project."""

from __future__ import annotations

import json
import math
import os
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
        fixture = project / "test/fixtures/audio-smoke.wav"
        fixture.parent.mkdir(parents=True, exist_ok=True)
        samples = [int(6000 * math.sin(2.0 * math.pi * 440.0 * frame / 8000)) for frame in range(400)]
        with wave.open(str(fixture), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8000)
            output.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))
        runner = ROOT / "scripts/elisa_build_run.py"
        settings = {
            "title": "Elisa Engine Smoke",
            "width": 320,
            "height": 200,
            "hidden": True,
        }
        projects = [
            ("application-native-smoke", ROOT / "test/application_native_main.elisa"),
            ("physics-render-capture-smoke", ROOT / "test/physics_render_capture_native.elisa"),
            ("application-failure-cleanup-smoke", ROOT / "test/application_failure_native_main.elisa"),
        ]
        physics_captures = ROOT / "build/validation/physics-render-cadence"
        physics_captures.mkdir(parents=True, exist_ok=True)
        for capture_name in ("physics-30hz.png", "physics-120hz.png"):
            (physics_captures / capture_name).unlink(missing_ok=True)
        native_test = project / "user-data-native-test"
        native_command = [
            "clang++", "-std=c++17", "-O0",
            str(ROOT / "native/user_data_abi.cpp"),
            str(ROOT / "test/user_data_service_test.cpp"),
            "-o", str(native_test),
        ]
        compiled = subprocess.run(native_command, check=False)
        if compiled.returncode != 0:
            print("Native user-data service test did not compile.", file=sys.stderr)
            return compiled.returncode
        tested = subprocess.run([str(native_test)], check=False)
        if tested.returncode != 0:
            print("Native user-data service test failed.", file=sys.stderr)
            return tested.returncode

        for name, entry in projects:
            manifest = {
                "name": name,
                "main": str(entry),
                "output": f"build/{name}",
                "application": settings,
            }
            (project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
            command = [sys.executable, str(runner), "run", "--project", str(project),
                "--native-test-probes"]
            environment = dict(os.environ)
            environment["ELISA_USER_DATA_DIR"] = str(project / "user-data")
            screenshot = project / f"{name}-frame.png"
            environment["ELISA_SMOKE_SCREENSHOT_PATH"] = str(screenshot)
            environment["ELISA_PHYSICS_30HZ_CAPTURE_PATH"] = str(physics_captures / "physics-30hz.png")
            environment["ELISA_PHYSICS_120HZ_CAPTURE_PATH"] = str(physics_captures / "physics-120hz.png")
            status = subprocess.run(command, env=environment, check=False).returncode
            if status == 0 and name == "application-native-smoke":
                header = screenshot.read_bytes()[:8] if screenshot.exists() else b""
                if header != b"\x89PNG\r\n\x1a\n":
                    print("Native application smoke did not write a PNG screenshot.", file=sys.stderr)
                    return 1
            if status == 0 and name == "physics-render-capture-smoke":
                captures = [physics_captures / "physics-30hz.png",
                    physics_captures / "physics-120hz.png"]
                image_data = []
                image_sizes = []
                for cadence_path in captures:
                    data = cadence_path.read_bytes() if cadence_path.exists() else b""
                    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
                        print(f"Physics render cadence smoke did not write a valid {cadence_path.name} PNG.", file=sys.stderr)
                        return 1
                    image_sizes.append((int.from_bytes(data[16:20], "big"),
                        int.from_bytes(data[20:24], "big")))
                    image_data.append(data)
                if image_sizes[0][0] <= 0 or image_sizes[0][1] <= 0 or image_sizes[0] != image_sizes[1]:
                    print("Physics cadence captures have invalid or mismatched dimensions.", file=sys.stderr)
                    return 1
                if image_data[0] != image_data[1]:
                    print("30 Hz and 120 Hz physics-to-Wicked captures differ.", file=sys.stderr)
                    return 1
                print(f"Physics-to-Wicked captures match exactly at {image_sizes[0][0]}x{image_sizes[0][1]} pixels.")
            if status != 0:
                print(f"Native application smoke {name} failed with status {status}.", file=sys.stderr)
                return status
            print(f"Native application smoke {name} passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
