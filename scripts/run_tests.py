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


def setup_remotes(a, root_scratch, local_sha):
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
        errors = list(ex.map(lambda r: r.probe(local_sha, triple), remotes))
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
    ap.add_argument("--local-compiles", type=int, help="local compile workers beside live remotes (default: idle local cores)")
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
    remotes, linker = setup_remotes(a, scratch, compiler_sha)
    identities = [compiler_sha] + sorted({r.identity for r in remotes})

    durations_path = cache / "durations.json"
    try:
        durations = json.loads(durations_path.read_text())
    except (OSError, ValueError):
        durations = {}
    began = {}
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
            # Last measured compile seconds, else include bytes as a proxy (scaled far below any timing).
            todo.append((durations.get(t["name"], sum(f.stat().st_size for f in files) * 1e-9), i, keys))
    todo.sort(reverse=True)  # slowest first: remotes take from the front, local workers from the back
    local_only = collections.deque()
    queue, lock = collections.deque((i, keys) for _, i, keys in todo), threading.Lock()
    pending, settled = len(queue), threading.Condition(lock)  # compiles not yet built or failed
    runner = cf.ThreadPoolExecutor(max_workers=max(1, a.j))
    futures = {}

    def run(i):
        t = tests[i]
        for attempt in range(3):  # 126 with no output = spawn failure under swap pressure
            p = subprocess.run([str(ROOT / t["binary"]), *t["args"]], capture_output=True, text=True, cwd=ROOT)
            if p.returncode != 126 or p.stdout or p.stderr:
                break
        return t, p.returncode, p.stdout, p.stderr, stage[i]

    def settle():
        nonlocal pending
        with lock:
            pending -= 1
            settled.notify_all()

    def done(i, keys, ident, how):
        durations[tests[i]["name"]] = round(time.monotonic() - began.get(i, time.monotonic()), 1)
        (cache / (Path(tests[i]["binary"]).name + ".key")).write_text(keys[ident])
        stage[i] = how
        futures[i] = runner.submit(run, i)
        settle()

    def fail(i, rc, out, err):
        results[i] = (tests[i], rc, out, err, "compile")
        settle()

    def local_worker(retries_only=False):
        while True:
            with lock:
                if not local_only and (retries_only or not queue):
                    return
                i, keys = local_only.pop() if local_only else queue.pop()
                began[i] = time.monotonic()
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

    def remote_host(r):
        try:
            err = r.sync(ROOT)
            if err:
                print(f"remote compile: dropping {r.name}: {err}", file=sys.stderr)
                return
            pool = [threading.Thread(target=remote_worker, args=(r, w)) for w in range(r.slots)]
            for w in pool:
                w.start()
            for w in pool:
                w.join()
        finally:
            r.alive = False
            with lock:
                settled.notify_all()

    def remote_worker(r, w):
        triple = remote_compile.mac_triple()
        while r.alive:
            with lock:
                if not queue:
                    return
                i, keys = queue.popleft()
                began[i] = time.monotonic()
            rc, log, obj = r.compile(w, tests[i]["flags"], tests[i]["source"], triple)
            ok = False
            if rc is None:
                if r.alive:
                    print(f"remote compile: dropping {r.name}: {log.strip()[-200:]}", file=sys.stderr)
                r.alive = False
            elif rc == 0 and obj:
                o = Path(scratch) / f"{i}.o"
                o.write_bytes(obj)
                ok = linker.link(o, ROOT / tests[i]["binary"]).returncode == 0
                o.unlink()
            if ok:
                done(i, keys, r.identity, "built remotely")
            elif rc is None:
                with lock:
                    queue.appendleft((i, keys))  # the host failed: another host (or local) takes it
                    settled.notify_all()
            else:
                with lock:
                    local_only.append((i, keys))  # a compile or link failure is decided by the local compiler
                    settled.notify_all()

    for i in stage:
        futures[i] = runner.submit(run, i)
    workers = [threading.Thread(target=remote_host, args=(r,), daemon=True) for r in remotes]
    if remotes:  # the Mac only links, unless asked to compile too
        workers += [threading.Thread(target=local_worker, daemon=True) for _ in range(a.local_compiles if a.local_compiles is not None
                                                                            else max(0, int(os.cpu_count() - os.getloadavg()[0])))]
    for w in workers:  # daemon: a host stuck in rsync must not hold the run once everything is built
        w.start()
    while True:
        with lock:  # wake when all is built, or when work is left that no remote host will take
            settled.wait_for(lambda: pending == 0 or local_only or (queue and not any(r.alive for r in remotes)))
            if pending == 0:
                built_at = time.monotonic() - started
                break
            retries_only = any(r.alive for r in remotes)
        local = [threading.Thread(target=local_worker, args=(retries_only,)) for _ in range(max(1, a.j))]
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

    durations_path.write_text(json.dumps(durations, indent=0, sort_keys=True))
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
          f"status {status}, {time.monotonic() - started:.0f}s (compiles done at {built_at:.0f}s)")
    return status


if __name__ == "__main__":
    sys.exit(main())
