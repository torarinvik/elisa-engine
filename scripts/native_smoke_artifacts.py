#!/usr/bin/env python3
"""Structured result artifacts for native application smokes.

Each smoke writes `build/native-smoke/NAME.json` and `NAME.log`. A failing
status is resolved to the `return STATUS if CONDITION` line in the smoke's
Elisa source, so a failure names the assertion that tripped, not just a number.
"""

from __future__ import annotations

import json
import re
import sys
import tempfile
import time
from pathlib import Path

LOG_TAIL_LINES = 40


def failed_assertions(source: Path, status: int) -> list[dict[str, object]]:
    """Every `return STATUS if ...` guard in the source, with its line number."""
    if status == 0 or not source.is_file():
        return []
    pattern = re.compile(rf"^\s*return {status} if (.+?)\s*$")
    found = []
    for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), start=1):
        match = pattern.match(line)
        if match:
            found.append({"line": number, "condition": match.group(1)})
    return found


def record(directory: Path, name: str, source: Path, status: int,
        elapsed_seconds: float, output: str) -> Path:
    """Write NAME.json and NAME.log under `directory`; returns the JSON path."""
    directory.mkdir(parents=True, exist_ok=True)
    log_path = directory / f"{name}.log"
    log_path.write_text(output, encoding="utf-8")
    result = {
        "name": name,
        "source": str(source),
        "status": status,
        "passed": status == 0,
        "elapsed_seconds": round(elapsed_seconds, 3),
        "recorded_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "failed_assertions": failed_assertions(source, status),
        "log": log_path.name,
        "log_tail": output.splitlines()[-LOG_TAIL_LINES:] if status != 0 else [],
    }
    json_path = directory / f"{name}.json"
    json_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return json_path


def self_test() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        source = root / "smoke.elisa"
        source.write_text("def f() -> i32:\n    return 44 if not ok()\n    return 4 if other\n"
            "    return 44 if again()\n    0\n", encoding="utf-8")
        failed = json.loads(record(root / "out", "demo", source, 44, 1.25, "a\nb\n").read_text())
        assert failed["passed"] is False and failed["status"] == 44
        assert failed["failed_assertions"] == [
            {"line": 2, "condition": "not ok()"}, {"line": 4, "condition": "again()"}]
        assert failed["log_tail"] == ["a", "b"]
        assert (root / "out/demo.log").read_text() == "a\nb\n"
        passed = json.loads(record(root / "out", "ok", source, 0, 0.5, "fine\n").read_text())
        assert passed["passed"] and passed["failed_assertions"] == [] and passed["log_tail"] == []
        missing = json.loads(record(root / "out", "gone", root / "none.elisa", 3, 0.1, "").read_text())
        assert missing["failed_assertions"] == []
    print("Native smoke artifact self-test passed.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        raise SystemExit(self_test())
    print("usage: native_smoke_artifacts.py --self-test", file=sys.stderr)
    raise SystemExit(2)
