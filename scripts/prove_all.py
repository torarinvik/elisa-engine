#!/usr/bin/env python3
"""Prove proof/*.elisa in parallel with a content cache, optionally sharing work with a Linux host.

A proof's cache key hashes the prover binary and every file its includes reach, so an unchanged
proof is answered from build/proof-cache without running the prover. Misses run on local workers
(-j, default all cores) and, with --remote HOST:PROVER, on remote workers (-r) after the tree's
proof/ and src/ are rsynced to HOST:~/work/elisa-engine-runner/tree. Each report is written to
build/<name>-proof.json exactly as scripts/check.elisascript expects. Exit status is the first
failing proof's status, else 0.
"""
import argparse, concurrent.futures as cf, hashlib, os, re, subprocess, sys, threading, time
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
    ap.add_argument("--remote", default=os.environ.get("ELISA_PROOF_REMOTE"),
                    help="HOST:PATH of a Linux prover built from the same revision (env ELISA_PROOF_REMOTE)")
    ap.add_argument("--remote-prover-sha", help="cache identity of the remote prover (default: local prover's)")
    ap.add_argument("-r", type=int, default=128, help="max workers per remote (capped by its cores and free GB)")
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

    # Remotes: comma-separated HOST[#PORT]:PATH entries; each gets workers sized to its free memory.
    remotes = []
    for spec in (a.remote.split(",") if a.remote and todo else []):
        target, path = spec.split(":", 1)
        host, _, port = target.partition("#")
        ssh = ["ssh", "-o", "ConnectTimeout=5", "-o", "LogLevel=ERROR", "-o", "ControlMaster=auto",
               "-o", f"ControlPath={os.path.expanduser('~')}/.ssh/cm-%C", "-o", "ControlPersist=120"] + (["-p", port] if port else [])
        probe = subprocess.run(ssh + [host, # free GB = min(MemAvailable, cgroup memory.max - memory.current) when a limit is set
                                       "m=$(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo); "
                                       "l=$(cat /sys/fs/cgroup/memory.max 2>/dev/null); c=$(cat /sys/fs/cgroup/memory.current 2>/dev/null); "
                                       "if [ -n \"$l\" ] && [ \"$l\" != max ]; then g=$(( (l - c) / 1073741824 )); [ $g -lt $m ] && m=$g; fi; echo $m; "
                                       # cgroup quota, not nproc: rented containers report host cores
                                       "awk '{print ($1==\"max\") ? n : int($1/$2)}' n=$(nproc) /sys/fs/cgroup/cpu.max 2>/dev/null || nproc"],
                               capture_output=True, text=True)
        try:
            mem, cores = (int(x) for x in probe.stdout.split()[-2:])
        except ValueError:
            print(f"remote {host} unreachable; skipping", flush=True)
            continue
        workers = min(a.r, mem - 1, cores)
        if workers <= 0:
            print(f"remote {host} short of memory; skipping", flush=True)
            continue
        subprocess.run(ssh + [host, f"mkdir -p {REMOTE_TREE}"], check=True)
        subprocess.run(["rsync", "-az", "--delete", "-e", " ".join(ssh), "--include=*/", "--include=*.elisa",
                        "--exclude=*", "proof", "src", f"{host}:{REMOTE_TREE}/"], cwd=ROOT, check=True)
        remotes.append((host, ssh, path, workers))

    queue, lock = list(todo), threading.Lock()

    def take():
        with lock:
            return queue.pop(0) if queue else None

    def worker(remote):
        while (item := take()):
            n, key = item
            if remote:
                host, ssh, path, _ = remote
                cmd = ssh + [host, f"cd {REMOTE_TREE} && {path} --json proof/{n}.elisa"]
            else:
                cmd = [a.prover, "--json", str(ROOT / "proof" / f"{n}.elisa")]
            t0 = time.time()
            for _ in range(3):  # 255 is ssh itself failing (connection limits), not the prover
                p = subprocess.run(cmd, capture_output=True, text=True)
                if not remote or p.returncode != 255:
                    break
            took = time.time() - t0
            where = remote[0] if remote else "local"
            with lock:
                results[n] = (p.returncode, p.stdout, p.stderr, where)
                print(f"{'ok ' if p.returncode == 0 else 'FAIL'} {n} [{where}] {took:.1f}s", flush=True)
            if p.returncode == 0:
                (cache / f"{key}.json").write_text(p.stdout)

    slots = [None] * min(a.j, len(todo)) + [r for r in remotes for _ in range(min(r[3], len(todo)))]
    with cf.ThreadPoolExecutor(max_workers=max(1, len(slots))) as ex:
        futs = [ex.submit(worker, slot) for slot in slots]
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
