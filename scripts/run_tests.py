#!/usr/bin/env python3
"""Compile and run the gate's Elisa tests (scripts/gate_tests.json) in parallel with a compile cache.

A compile is skipped when build/test-cache/<binary name>.key holds the same key: sha256 of the
compiler binary, the test's flags and every file its include closure reaches. Some tests keep
-O0 in their flags so large fixtures stay inside the 120-second process limit. Binaries always
run (in parallel, cwd = repo root). Exit status is the first failing test's status in manifest
order, with that test's output printed; else 0.

With --remote / ELISA_COMPILE_REMOTE (see scripts/remote_compile.py) compiles also go to Linux
hosts that cross-compile Mac objects; linking and running stay here. A remotely built binary's
key uses the remote compiler's identity, so either compiler's binary satisfies the cache. A
remote compile failure is retried locally, so only the local compiler decides a failure.
"""
import argparse, collections, concurrent.futures as cf, hashlib, json, os, re, subprocess, sys, tempfile, threading, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import remote_compile

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


def setup_remotes(a, root_scratch):
    remotes = remote_compile.parse(a.remote)
    if not remotes:
        return [], None
    linker = remote_compile.Linker(a.compiler, root_scratch)
    if not linker.ok():
        print(f"remote compile: cannot reproduce the -emit exe link here (runtime {linker.runtime}); compiling locally",
              file=sys.stderr)
        return [], None
    triple = remote_compile.mac_triple()
    with cf.ThreadPoolExecutor(len(remotes)) as ex:
        errors = list(ex.map(lambda r: r.setup(ROOT, triple), remotes))
    live = []
    for r, err in zip(remotes, errors):
        if err:
            print(f"remote compile: dropping {r.name}: {err}", file=sys.stderr)
        else:
            live.append(r)
    return live, linker


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("compiler")
    ap.add_argument("--manifest", default=str(ROOT / "scripts/gate_tests.json"))
    ap.add_argument("-j", type=int, default=os.cpu_count(), help="parallel test runs (and local compiles without remotes)")
    ap.add_argument("--local-compiles", type=int, help="local compile workers (default -j, or 2 with live remotes)")
    ap.add_argument("--remote", default=os.environ.get("ELISA_COMPILE_REMOTE"),
                    help="HOST[#PORT]:STAGE1PATH,... Linux stage1 compilers (env ELISA_COMPILE_REMOTE)")
    ap.add_argument("--no-cache", action="store_true")
    a = ap.parse_args()

    started = time.monotonic()
    tests = json.loads(Path(a.manifest).read_text())
    compiler_sha = hashlib.sha256(Path(a.compiler).read_bytes()).hexdigest()
    cache = ROOT / "build/test-cache"
    cache.mkdir(parents=True, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix="run-tests-", dir=ROOT / "build")
    remotes, linker = setup_remotes(a, scratch)
    identities = [compiler_sha] + sorted({r.identity for r in remotes})

    stage, results, todo = {}, {}, []
    for i, t in enumerate(tests):
        binary, stamp = ROOT / t["binary"], cache / (Path(t["binary"]).name + ".key")
        keys = {ident: cache_key(ident, t) for ident in identities}
        if not a.no_cache and binary.exists() and stamp.exists() and stamp.read_text() in keys.values():
            stage[i] = "cached"
        else:
            stamp.unlink(missing_ok=True)
            files = set()
            closure(ROOT / t["source"], files)
            todo.append((sum(f.stat().st_size for f in files), i, keys))
    todo.sort(reverse=True)  # biggest first: remotes take from the front, local workers from the back
    queue, lock = collections.deque((i, keys) for _, i, keys in todo), threading.Lock()
    runner = cf.ThreadPoolExecutor(max_workers=max(1, a.j))
    futures = {}

    def run(i):
        t = tests[i]
        for attempt in range(3):  # 126 with no output = spawn failure under swap pressure
            p = subprocess.run([str(ROOT / t["binary"]), *t["args"]], capture_output=True, text=True, cwd=ROOT)
            if p.returncode != 126 or p.stdout or p.stderr:
                break
        return t, p.returncode, p.stdout, p.stderr, stage[i]

    def done(i, keys, ident, how):
        (cache / (Path(tests[i]["binary"]).name + ".key")).write_text(keys[ident])
        stage[i] = how
        futures[i] = runner.submit(run, i)

    def fail(i, rc, out, err):
        results[i] = (tests[i], rc, out, err, "compile")

    def local_worker():
        while True:
            with lock:
                if not queue:
                    return
                i, keys = queue.pop()
            t = tests[i]
            cmd = [a.compiler, "-emit", "exe", *t["flags"], "-o", str(ROOT / t["binary"]), str(ROOT / t["source"])]
            for attempt in range(3):  # 126 with no output = spawn failure under swap pressure
                p = subprocess.run(cmd, capture_output=True, text=True)
                if p.returncode != 126 or p.stdout or p.stderr:
                    break
            if p.returncode != 0:
                fail(i, p.returncode, p.stdout, p.stderr)
            else:
                done(i, keys, compiler_sha, "built")

    def remote_worker(r):
        while r.alive:
            with lock:
                batch = [queue.popleft() for _ in range(min(r.batch_size(), len(queue)))]
            if not batch:
                return
            got = r.compile([(tests[i]["flags"], tests[i]["source"]) for i, _ in batch], remote_compile.mac_triple())
            retry = []
            if got is None:
                r.alive = False
                print(f"remote compile: {r.name} failed a batch; its tests compile locally", file=sys.stderr)
                retry = batch
            else:
                for (i, keys), (rc, log, obj) in zip(batch, got):
                    if rc != 0 or not obj:
                        retry.append((i, keys))
                        continue
                    o = Path(scratch) / f"{i}.o"
                    o.write_bytes(obj)
                    p = linker.link(o, ROOT / tests[i]["binary"])
                    o.unlink()
                    if p.returncode != 0:
                        retry.append((i, keys))
                    else:
                        done(i, keys, r.identity, "built remotely")
            with lock:
                queue.extend(retry)  # back of the queue: local workers take these first

    local_n = a.local_compiles if a.local_compiles is not None else (2 if remotes else a.j)
    workers = [threading.Thread(target=remote_worker, args=(r,)) for r in remotes for _ in range(remote_compile.CHANNELS)]
    for i in stage:
        futures[i] = runner.submit(run, i)
    for w in workers:
        w.start()
    local = [threading.Thread(target=local_worker) for _ in range(max(1 if not workers else 0, local_n))]
    for w in local:
        w.start()
    for w in workers + local:
        w.join()
    if queue:  # every remote died and no local worker was requested: finish here
        local = [threading.Thread(target=local_worker) for _ in range(max(1, a.j))]
        for w in local:
            w.start()
        for w in local:
            w.join()
    for i, f in futures.items():
        results[i] = f.result()
    runner.shutdown()
    for f in Path(scratch).iterdir():
        f.unlink()
    os.rmdir(scratch)

    status, hits = 0, 0
    for i in range(len(tests)):
        t, rc, out, err, how = results[i]
        hits += how == "cached"
        if rc != 0:
            if status == 0:
                sys.stdout.write(out)
                sys.stderr.write(err)
                print(f"FAIL {t['name']} ({how}) rc={rc}: {t['source']}", file=sys.stderr)
                status = rc
            else:
                print(f"FAIL {t['name']} ({how}) rc={rc}", file=sys.stderr)
        elif t.get("message"):
            print(t["message"])
    remote_n = sum(1 for i in range(len(tests)) if results[i][4] == "built remotely")
    print(f"tests: {len(tests)} total, {hits} compiles cached, {remote_n} compiled remotely, "
          f"status {status}, {time.monotonic() - started:.0f}s")
    return status


if __name__ == "__main__":
    sys.exit(main())
