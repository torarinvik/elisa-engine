#!/usr/bin/env python3
"""Qualify actual default CLI admission of mutable-global grants.

Run separately when evaluating a replacement compiler. Passing these source
checks does not qualify code generation, runtime identity, or native compatibility.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path
import subprocess
import tempfile


GLOBAL = "global mutable count: i64 = 0\n"
CASES = (
    ("missing-read", "def read_count() -> i64:\n    return count\n", "Global.Read"),
    ("missing-write", "def write_count() -> i64:\n    count <- 1\n    return 0\n", "Global.Write"),
    ("read-granted", "def read_count() -> i64:\n    can Global.Read:\n        return count\n", ""),
    ("write-granted", "def write_count() -> i64:\n    can Global.Write:\n        count <- 1\n        return 0\n", ""),
    ("rmw-read-only", "def increment() -> i64:\n    can Global.Read:\n        count <- count + 1\n        return 0\n", "Global.Write"),
    ("rmw-write-only", "def increment() -> i64:\n    can Global.Write:\n        count <- count + 1\n        return 0\n", "Global.Read"),
    ("rmw-both", "def increment() -> i64:\n    can Global{Read,Write}:\n        count <- count + 1\n        return count\n", ""),
    ("signature-is-not-local-grant", "def read_count() -> i64 can[Global.Read]:\n    return count\n", "Global.Read"),
    ("local-grants", "def increment() -> i64:\n    can Global.Read, Global.Write:\n        count <- count + 1\n        return count\n", ""),
    ("shadowed-local", "def local(count: i64) -> i64:\n    return count\n", ""),
    ("transitive-missing", "def read_count() -> i64 can[Global.Read]:\n    return count\ndef caller() -> i64:\n    return read_count()\n", "Global.Read"),
    ("transitive-granted", "def read_count() -> i64 can[Global.Read]:\n    can Global.Read:\n        return count\ndef caller() -> i64:\n    can Global.Read:\n        return read_count()\n", ""),
)


def qualify(compiler: str, directory: Path, timeout: float = 60) -> list[dict]:
    records = []
    for name, body, permission in CASES:
        source = directory / f"{name}.elisa"
        source.write_text(GLOBAL + body, encoding="utf-8")
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        for permissive in (False, True):
            command = [compiler, "-emit", "check"]
            if permissive:
                command.append("-permissive")
            command.append(str(source))
            expected_accept = not permission or permissive
            try:
                result = subprocess.run(command, capture_output=True, text=True,
                    check=False, timeout=timeout)
                diagnostic = result.stdout + result.stderr
                accepted = result.returncode == 0
                passed = accepted == expected_accept
                # A missing compiler, parse error or unrelated failure is not a grant refusal.
                if not expected_accept:
                    passed = passed and result.returncode == 1 and permission in diagnostic
                    passed = passed and ("requires" in diagnostic or "required" in diagnostic
                        or f"accesses a global mutable binding without {permission}" in diagnostic)
                record = {"status": result.returncode, "diagnostic": diagnostic, "passed": passed}
            except (OSError, subprocess.TimeoutExpired) as error:
                record = {"status": None, "diagnostic": str(error), "passed": False}
            record.update(case=name, permissive=permissive, expected_accept=expected_accept,
                required_permission=permission, source_sha256=source_hash)
            if hashlib.sha256(source.read_bytes()).hexdigest() != source_hash:
                record["passed"] = False
                record["diagnostic"] += "\nqualification source changed during check"
            records.append(record)
    return records


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, help="Actual candidate compiler executable or launcher")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--product", type=Path, help="Underlying compiler binary when --compiler is a launcher")
    parser.add_argument("--expected-product-sha256", help="Require this exact compiler product hash")
    args = parser.parse_args(argv)
    located = shutil.which(args.compiler)
    product = args.product or (Path(located) if located else Path(args.compiler))
    records = []
    before = after = None
    input_error = ""
    try:
        before = hashlib.sha256(product.read_bytes()).hexdigest()
        expected = args.expected_product_sha256
        if expected is not None and (len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected)):
            input_error = "expected product hash must be 64 lowercase hexadecimal characters"
        elif expected is not None and before != expected:
            input_error = "compiler product does not match expected hash"
        else:
            with tempfile.TemporaryDirectory(prefix="elisa-global-grant-qualification-") as temporary:
                records = qualify(args.compiler, Path(temporary))
            after = hashlib.sha256(product.read_bytes()).hexdigest()
            if after != before:
                input_error = "compiler product changed during qualification"
    except OSError as error:
        input_error = f"could not read compiler product: {error}"
    passed = bool(records) and not input_error and all(row["passed"] for row in records)
    report = {"schema": "elisa-global-grant-cli-qualification-v1", "compiler": args.compiler,
        "passed": passed, "scope": "source admission only; runtime/native promotion not established",
        "product": str(product), "product_sha256_before": before, "product_sha256_after": after,
        "expected_product_sha256": args.expected_product_sha256, "input_error": input_error,
        "cases": records}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Global grant CLI qualification: {'PASS' if passed else 'FAIL'}; {sum(row['passed'] for row in records)}/{len(records)} controls pass")
    if input_error:
        print(input_error)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
