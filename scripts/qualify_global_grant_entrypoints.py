#!/usr/bin/env python3
"""Strictly compile native, probe, and example entrypoints outside the engine manifest."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import re
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "scripts/gate_tests.json"
ENTRYPOINT = re.compile(r"^def\s+main\s*\(", re.M)
INCLUDE = re.compile(r'^\s*include\s+"([^"]+)"', re.M)
EXPLICIT_ENTRYPOINTS = ("examples/maze/capi.elisa",)


def source_paths(root: Path = ROOT, manifest: Path = DEFAULT_MANIFEST) -> list[str]:
    """Find ordinary app/test mains not already compiled by the main test manifest."""
    tests = json.loads(manifest.read_text(encoding="utf-8"))
    manifest_sources = {Path(test["source"]).as_posix() for test in tests}
    selected: set[str] = set()
    for base in ("examples", "test"):
        for path in sorted((root / base).rglob("*.elisa")):
            relative = path.relative_to(root).as_posix()
            if relative in manifest_sources or relative.startswith("test/negative/"):
                continue
            if ENTRYPOINT.search(path.read_text(encoding="utf-8", errors="replace")):
                selected.add(relative)
    for relative in EXPLICIT_ENTRYPOINTS:
        if (root / relative).is_file() and relative not in manifest_sources:
            selected.add(relative)
    return sorted(selected)


def source_closure_hash(roots: list[Path]) -> str:
    """Fingerprint every recursively included source used by one wrapped entrypoint."""
    seen: set[Path] = set()

    def visit(path: Path) -> None:
        path = path.resolve()
        if path in seen or not path.is_file():
            return
        seen.add(path)
        source = path.read_text(encoding="utf-8", errors="replace")
        for included in INCLUDE.findall(source):
            visit(path.parent / included)

    for root in roots:
        visit(root)
    digest = hashlib.sha256()
    for path in sorted(seen):
        digest.update(str(path).encode("utf-8"))
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def compile_entrypoints(compiler: str, root: Path, sources: list[str], scratch: Path,
    jobs: int = 6, timeout: float = 60) -> list[dict]:
    runtime = (root / "src/runtime/public.elisa").resolve()

    def compile_one(relative: str) -> dict:
        source = (root / relative).resolve()
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        closure_hash = source_closure_hash([runtime, source])
        with tempfile.NamedTemporaryFile("w", suffix=".elisa", prefix="grant-entrypoint-",
            dir=scratch, encoding="utf-8", delete=False) as temporary:
            wrapper = Path(temporary.name)
            temporary.write(f'include "{runtime}"\ninclude "{source}"\n')
        command = [compiler, "-emit", "check", str(wrapper)]
        try:
            result = subprocess.run(command, capture_output=True, text=True, check=False,
                timeout=timeout)
            diagnostic = result.stdout + result.stderr
            returncode = result.returncode
        except (OSError, subprocess.TimeoutExpired) as error:
            diagnostic = str(error)
            returncode = None
        finally:
            wrapper.unlink(missing_ok=True)
        source_unchanged = (hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
            and source_closure_hash([runtime, source]) == closure_hash)
        if not source_unchanged:
            diagnostic += "\nentrypoint source or include closure changed during qualification"
        return {"source": relative, "source_sha256": source_hash,
            "source_closure_sha256": closure_hash,
            "source_unchanged": source_unchanged, "returncode": returncode,
            "passed": returncode == 0 and source_unchanged, "diagnostic": diagnostic.strip()}

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        return list(pool.map(compile_one, sources))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, help="Strict Elisa compiler executable")
    parser.add_argument("--expected-product-sha256", required=True,
        help="Require the exact compiler product used by the engine runner")
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST,
        help="Engine test manifest whose sources are already checked by the main gate")
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args(argv)

    entries: list[dict] = []
    before = after = None
    input_error = ""
    manifest_entries = 0
    started = time.monotonic()
    compiler_path = Path(args.compiler)
    try:
        before = hashlib.sha256(compiler_path.read_bytes()).hexdigest()
        expected = args.expected_product_sha256
        if len(expected) != 64 or any(char not in "0123456789abcdef" for char in expected):
            input_error = "expected product hash must be 64 lowercase hexadecimal characters"
        elif before != expected:
            input_error = "compiler product does not match expected hash"
        else:
            manifest_entries = len(json.loads(args.manifest.read_text(encoding="utf-8")))
            sources = source_paths(ROOT, args.manifest.resolve())
            if not sources:
                input_error = "no additional entrypoints were found"
            else:
                scratch_root = ROOT / "build/validation"
                scratch_root.mkdir(parents=True, exist_ok=True)
                with tempfile.TemporaryDirectory(prefix="global-grant-entrypoints-",
                    dir=scratch_root) as temporary:
                    entries = compile_entrypoints(args.compiler, ROOT, sources,
                        Path(temporary), args.jobs, args.timeout)
                after = hashlib.sha256(compiler_path.read_bytes()).hexdigest()
                if after != before:
                    input_error = "compiler product changed during qualification"
    except (OSError, ValueError, KeyError, TypeError) as error:
        input_error = f"could not qualify entrypoints: {error}"

    passed = bool(entries) and not input_error and all(entry["passed"] for entry in entries)
    report = {"schema": "elisa-global-grant-entrypoint-qualification-v1",
        "compiler": args.compiler, "compiler_sha256_before": before,
        "compiler_sha256_after": after, "expected_product_sha256": args.expected_product_sha256,
        "compiler_unchanged": before is not None and before == after,
        "permissive": False, "wrapper": "include src/runtime/public.elisa, then entrypoint source",
        "manifest": str(args.manifest.resolve()),
        "manifest_entries_excluded": manifest_entries,
        "entrypoint_count": len(entries), "passed": passed,
        "seconds": round(time.monotonic() - started, 1), "input_error": input_error,
        "entrypoints": entries}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Global grant entrypoint qualification: {'PASS' if passed else 'FAIL'}; "
        f"{sum(entry['passed'] for entry in entries)}/{len(entries)} strict checks pass")
    if input_error:
        print(input_error)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
