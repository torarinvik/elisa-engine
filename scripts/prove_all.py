#!/usr/bin/env python3
"""Prove proof/*.elisa in parallel with a content cache, optionally sharing work with a Linux host.

A proof's cache key hashes the prover binary and every file its includes reach, so an unchanged
proof is answered from build/proof-cache without running the prover. Misses run on local workers
(-j, default all cores) and, with --remote HOST:PROVER, on remote workers (-r) after the tree's
proof/ and src/ are rsynced to HOST:~/work/elisa-engine-runner/tree. Each report is written to
build/<name>-proof.json exactly as scripts/check.elisascript expects. Exit status is the first
failing proof's status, else 0.
"""
import argparse, concurrent.futures as cf, hashlib, os, re, subprocess, sys, threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = re.compile(r'^\s*include\s+"([^"]+)"', re.M)
REMOTE_TREE = "work/elisa-engine-runner/tree"


def closure(path, seen):
    path = path.resolve()
    if path in seen or not path.exists():
        return
    seen.add(path)
    for rel in INCLUDE.findall(path.read_text(errors="replace")):
        closure(path.parent / rel, seen)


def cache_key(prover_sha, proof):
    files = set()
    closure(proof, files)
    h = hashlib.sha256(prover_sha.encode())
    for f in sorted(files):
        h.update(str(f.relative_to(ROOT)).encode())
        h.update(hashlib.sha256(f.read_bytes()).digest())
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("prover")
    ap.add_argument("names", nargs="*", help="proof names (default: all of proof/)")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    ap.add_argument("--remote", help="HOST:PATH of a Linux prover built from the same revision")
    ap.add_argument("--remote-prover-sha", help="cache identity of the remote prover (default: local prover's)")
    ap.add_argument("-r", type=int, default=6, help="remote workers")
    ap.add_argument("--no-cache", action="store_true")
    a = ap.parse_args()

    names = a.names or sorted(p.stem for p in (ROOT / "proof").glob("*.elisa"))
    prover_sha = hashlib.sha256(Path(a.prover).read_bytes()).hexdigest()
    cache = ROOT / "build/proof-cache"
    cache.mkdir(parents=True, exist_ok=True)
    todo, results = [], {}
    for n in names:
        key = cache_key(prover_sha, ROOT / "proof" / f"{n}.elisa")
        hit = cache / f"{key}.json"
        if hit.exists() and not a.no_cache:
            results[n] = (0, hit.read_text(), "", "cache")
        else:
            todo.append((n, key))

    remote_host = None
    if a.remote and todo:
        remote_host, remote_prover = a.remote.split(":", 1)
        subprocess.run(["ssh", remote_host, f"mkdir -p {REMOTE_TREE}"], check=True)
        subprocess.run(["rsync", "-az", "--delete", "--include=*/", "--include=*.elisa", "--exclude=*",
                        "proof", "src", f"{remote_host}:{REMOTE_TREE}/"], cwd=ROOT, check=True)

    queue, lock = list(todo), threading.Lock()

    def take():
        with lock:
            return queue.pop(0) if queue else None

    def worker(remote):
        while (item := take()):
            n, key = item
            if remote:
                cmd = ["ssh", remote_host, f"cd {REMOTE_TREE} && {remote_prover} --json proof/{n}.elisa"]
            else:
                cmd = [a.prover, "--json", str(ROOT / "proof" / f"{n}.elisa")]
            p = subprocess.run(cmd, capture_output=True, text=True)
            where = remote_host if remote else "local"
            with lock:
                results[n] = (p.returncode, p.stdout, p.stderr, where)
                print(f"{'ok ' if p.returncode == 0 else 'FAIL'} {n} [{where}]", flush=True)
            if p.returncode == 0:
                (cache / f"{key}.json").write_text(p.stdout)

    with cf.ThreadPoolExecutor() as ex:
        futs = [ex.submit(worker, False) for _ in range(min(a.j, len(todo)))]
        if remote_host:
            futs += [ex.submit(worker, True) for _ in range(min(a.r, len(todo)))]
        for f in futs:
            f.result()

    status = 0
    for n in names:
        rc, out, err, where = results[n]
        (ROOT / "build" / f"{n.replace('_', '-')}-proof.json").write_text(out)
        if rc != 0:
            sys.stderr.write(err)
            print(f"proof failed: proof/{n}.elisa [{where}] rc={rc}", file=sys.stderr)
            status = status or rc
    hits = sum(1 for r in results.values() if r[3] == "cache")
    print(f"proofs: {len(names)} total, {hits} cached, {len(todo)} run, status {status}")
    return status


if __name__ == "__main__":
    sys.exit(main())
