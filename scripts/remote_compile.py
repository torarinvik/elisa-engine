"""Remote compile pool for run_tests.py: compile gate tests to Mac objects on Linux hosts.

ELISA_COMPILE_REMOTE (or --remote) is a comma-separated list of HOST[#PORT]:STAGE1PATH, the
same shape as ELISA_PROOF_REMOTE. STAGE1PATH is a Linux elisac-stage1 built from the same
compiler revision as the local one. It cross-compiles each test with
`-emit obj -target-triple <this Mac's triple>`; the object comes back over ssh and is linked
here exactly as stage1's `-emit exe` links (scripts/run_tests.py runs it locally as before).

Each host gets the .elisa/.inc files of src/, test/ and examples/ rsynced to ~/work/engine-compile/tree. Its worker
count is min(cgroup CPU quota, free GB / PEAK_GB), and every worker pulls single tests from the
shared queue, so faster hosts take more. Workers share ssh master connections, at most
PER_MASTER sessions each (sshd's MaxSessions default is 10); an ssh exit of 255 is retried. A
host that cannot be reached or set up is dropped and its work goes back to the queue.
"""
import os, platform, re, shlex, subprocess
from pathlib import Path

REMOTE_TREE = "work/engine-compile/tree"
SYNC_DIRS = ["src", "test", "examples"]
SYNC_GLOBS = ["*.elisa", "*.inc"]  # what includes reach; examples/ is mostly assets
PER_MASTER = 8
PEAK_GB = 1  # test/world.elisa at -O0, the largest compile, peaks near 0.9 GB


def mac_triple():
    major = platform.mac_ver()[0].split(".")[0] or "15"
    return f"{os.uname().machine}-apple-macosx{major}.0.0"


class Remote:
    def __init__(self, spec):
        host, self.stage1 = spec.split(":", 1)
        self.host, _, self.port = host.partition("#")
        self.name = host
        self.identity = None
        self.slots = 0
        self.alive = True

    def ssh(self, worker=0):
        cmd = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", "-o", "ServerAliveInterval=15",
               "-o", "ControlMaster=auto", "-o", f"ControlPath=/tmp/elisa-cc-{worker // PER_MASTER}-%C",
               "-o", "ControlPersist=120", "-o", "LogLevel=ERROR"]
        return cmd + (["-p", self.port] if self.port else []) + [self.host]

    def probe(self, local_sha, triple):
        """Size the host's pool and hash its compiler. Returns an error string or None."""
        probe = (f"mkdir -p ~/{REMOTE_TREE} && "
                 "{ awk '{print ($1==\"max\") ? n : int($1/$2)}' n=$(nproc) /sys/fs/cgroup/cpu.max 2>/dev/null || { q=$(cat /sys/fs/cgroup/cpu/cpu.cfs_quota_us 2>/dev/null); p=$(cat /sys/fs/cgroup/cpu/cpu.cfs_period_us 2>/dev/null); if [ -n \"$q\" ] && [ \"$q\" -gt 0 ]; then echo $((q / p)); else nproc; fi; }; }; "
                 "m=$(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo); "
                 "l=$(cat /sys/fs/cgroup/memory.max 2>/dev/null); c=$(cat /sys/fs/cgroup/memory.current 2>/dev/null); "
                 "if [ -n \"$l\" ] && [ \"$l\" != max ]; then g=$(( (l - c) / 1073741824 )); [ $g -lt $m ] && m=$g; fi; echo $m; "
                 f"{{ sha256sum {self.stage1} $(ldd {self.stage1} | awk '/libLLVM/{{print $3}}') "
                 f"| cut -d' ' -f1; }} | sha256sum | cut -d' ' -f1")
        try:
            p = subprocess.run(self.ssh() + [probe], capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired:
            return "probe timed out"
        lines = p.stdout.split()
        if p.returncode != 0 or len(lines) != 3:
            return f"probe failed rc={p.returncode}: {p.stderr.strip()[-200:]}"
        self.slots = max(0, min(int(lines[0]), (int(lines[1]) - 1) // PEAK_GB))
        if self.slots == 0:
            return "no free CPU or memory"
        # Tied to the local compiler too, so a new local compiler invalidates remote builds.
        self.identity = f"{local_sha}+remote:{lines[2]}:{triple}"
        return None

    def sync(self, root):
        """Open the ssh masters one at a time (many workers racing to become master would trip
        sshd's MaxStartups), then rsync the tree. Runs in the host's own thread."""
        rsh = shlex.join(self.ssh()[:-1])
        try:
            for m in range(0, self.slots, PER_MASTER):
                if subprocess.run(self.ssh(m) + ["true"], capture_output=True, timeout=60).returncode:
                    return "ssh master failed"
            r = subprocess.run(["rsync", "-az", "--delete", "--delete-excluded", "--prune-empty-dirs", "--timeout=60",
                                "-e", rsh, "--include=*/", *(f"--include={g}" for g in SYNC_GLOBS), "--exclude=*", *SYNC_DIRS,
                                f"{self.host}:{REMOTE_TREE}/"], cwd=root, capture_output=True, text=True, timeout=300)
        except subprocess.TimeoutExpired:
            return "rsync timed out"
        return f"rsync failed rc={r.returncode}: {r.stderr.strip()[-200:]}" if r.returncode else None

    def compile(self, worker, flags, source, triple):
        """Returns (rc, log, object bytes); rc None means the host failed, not the compile."""
        args = shlex.join(["-emit", "obj", "-target-triple", triple, *flags])
        script = (f"cd ~/{REMOTE_TREE} && f=$(mktemp) && unset ELISA_HOST_LINUX ELISA_HOST_X86_64 && "
                  f"{{ {self.stage1} {args} -o $f {shlex.quote(source)} 1>&2; rc=$?; "
                  f"[ $rc = 0 ] && cat $f; rm -f $f; exit $rc; }}")
        for _ in range(3):  # 255 is ssh itself failing (connection or session limits)
            try:
                p = subprocess.run(self.ssh(worker) + [script], capture_output=True, timeout=900)
            except subprocess.TimeoutExpired:
                return None, "remote compile timed out", None
            if p.returncode != 255:
                break
        if p.returncode == 255:
            return None, p.stderr.decode(errors="replace"), None
        return p.returncode, p.stderr.decode(errors="replace"), p.stdout


def parse(spec):
    return [Remote(s.strip()) for s in (spec or "").split(",") if s.strip()]


FALLBACK_SOURCE = "src/driver/elisac_link.elisa"
PUSH = re.compile(r'emit_tokens_push_sview\(&source, "((?:[^"\\]|\\.)*)"\)')


class Linker:
    """Links a stage1 object as `-emit exe` does (src/driver/elisac_link.elisa): host clang with
    -fno-builtin -Wl,-dead_strip, the object, the runtime object, the generated weak callback
    fallbacks and ELISA_STAGE1_LINK. Paths resolve the way scripts/elisac_stage1.sh resolves them."""

    def __init__(self, compiler, scratch):
        croot = Path(compiler).resolve().parent.parent
        llvm_bin = os.environ.get("ELISA_LLVM_BIN_DIR") or str(
            Path(os.environ.get("LLVM_CONFIG", "/opt/homebrew/opt/llvm/bin/llvm-config")).parent)
        clang = os.environ.get("ELISA_CLANG") or str(Path(llvm_bin) / "clang")
        self.clang = clang if os.access(clang, os.X_OK) else "clang"
        self.runtime = os.environ.get("ELISA_RUNTIME_OBJ") or str(croot / "build/runtime/elisacore_runtime.o")
        self.extra = shlex.split(os.environ.get("ELISA_STAGE1_LINK", ""))
        self.fallback = None
        src = croot / FALLBACK_SOURCE
        if src.exists():
            body = src.read_text().split("def write_executable_callback_fallback", 1)[-1]
            body = body.split("return write_file_bytes", 1)[0]
            text = "".join(bytes(m, "utf-8").decode("unicode_escape") for m in PUSH.findall(body))
            if "ELISA_WEAK" in text:
                self.fallback = Path(scratch) / "stage1-callback-fallback.c"
                self.fallback.write_text(text)

    def ok(self):
        return self.fallback is not None and Path(self.runtime).exists()

    def link(self, obj, exe):
        cmd = [self.clang, "-fno-builtin", "-Wl,-dead_strip", "-o", str(exe), str(obj), self.runtime,
               str(self.fallback), *self.extra]
        return subprocess.run(cmd, capture_output=True, text=True)
