#!/usr/bin/env python3
"""Compile and run the gate's Elisa tests (scripts/gate_tests.json) in parallel with a compile cache.

A compile is skipped when build/test-cache/<binary name>.key holds the same key: sha256 of the
compiler binary, the test's flags and every file its include closure reaches. Some tests keep
-O0 in their flags so large fixtures stay inside the 120-second process limit. Binaries always
run (in parallel, cwd = repo root). Exit status is the first failing test's status in manifest
order, with that test's output printed; else 0.
"""
import argparse, concurrent.futures as cf, hashlib, json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = re.compile(r'^\s*include\s+"([^"]+)"', re.M)


def closure(path, seen):
    path = path.resolve()
    if path in seen or not path.exists():
        return
    seen.add(path)
    for rel in INCLUDE.findall(path.read_text(errors="replace")):
        closure(path.parent / rel, seen)


def cache_key(compiler_sha, test):
    files = set()
    closure(ROOT / test["source"], files)
    h = hashlib.sha256(compiler_sha.encode())
    h.update(json.dumps(test["flags"]).encode())
    for f in sorted(files):
        h.update(str(f).encode())
        h.update(hashlib.sha256(f.read_bytes()).digest())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("compiler")
    ap.add_argument("--manifest", default=str(ROOT / "scripts/gate_tests.json"))
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--no-cache", action="store_true")
    a = ap.parse_args()

    tests = json.loads(Path(a.manifest).read_text())
    compiler_sha = hashlib.sha256(Path(a.compiler).read_bytes()).hexdigest()
    cache = ROOT / "build/test-cache"
    cache.mkdir(parents=True, exist_ok=True)

    def one(t):
        binary = ROOT / t["binary"]
        stamp = cache / (binary.name + ".key")
        key = cache_key(compiler_sha, t)
        cached = not a.no_cache and binary.exists() and stamp.exists() and stamp.read_text() == key
        if not cached:
            stamp.unlink(missing_ok=True)
            cmd = [a.compiler, "-emit", "exe", *t["flags"], "-o", str(binary), str(ROOT / t["source"])]
            for attempt in range(3):  # 126 with no output = spawn failure under swap pressure
                p = subprocess.run(cmd, capture_output=True, text=True)
                if p.returncode != 126 or p.stdout or p.stderr:
                    break
            if p.returncode != 0:
                return t, p.returncode, p.stdout, p.stderr, "compile"
            stamp.write_text(key)
        for attempt in range(3):
            p = subprocess.run([str(binary), *t["args"]], capture_output=True, text=True, cwd=ROOT)
            if p.returncode != 126 or p.stdout or p.stderr:
                break
        return t, p.returncode, p.stdout, p.stderr, "cached" if cached else "built"

    with cf.ThreadPoolExecutor(max_workers=max(1, a.j)) as ex:
        results = list(ex.map(one, tests))

    status, hits = 0, 0
    for t, rc, out, err, stage in results:
        hits += stage == "cached"
        if rc != 0:
            if status == 0:
                sys.stdout.write(out)
                sys.stderr.write(err)
                print(f"FAIL {t['name']} ({stage}) rc={rc}: {t['source']}", file=sys.stderr)
                status = rc
            else:
                print(f"FAIL {t['name']} ({stage}) rc={rc}", file=sys.stderr)
        elif t.get("message"):
            print(t["message"])
    print(f"tests: {len(tests)} total, {hits} compiles cached, status {status}")
    return status


if __name__ == "__main__":
    sys.exit(main())
