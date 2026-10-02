#!/usr/bin/env python3
"""Start the native maze from packaged shaders only, then measure cold/warm hitches (R13).

The maze executable, its ELPK bundles and a packaged Metal shader library with
its schema 2 manifest are staged outside the checkout. Every launch runs under
a sandbox that denies the whole projects directory (so Wicked's shader sources
and every sibling checkout are unreadable), denies writes to the packaged
shader directory (so a runtime shader compile cannot land), and uses an
isolated HOME and TMPDIR. A launch that needed a source shader would fail
instead of silently compiling one.

The pipeline archive is keyed by the verified manifest bytes, the backend and
the engine build identity. The smoke proves that key in the archive identity,
that a warm launch loads the archive, that a changed shader package rejects
it, and that a tampered packaged shader stops startup.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from macos_bundle_support import SHADER_MANIFEST_NAME, shader_manifest
import packaged_maze_smoke

ROOT = Path(__file__).resolve().parents[1]
HITCHES = re.compile(r"elisa frame hitches: pipelines_us=(-?\d+) first_frame_us=(-?\d+) "
    r"later_max_us=(\d+) frames=(\d+)")
LOADED = "Metal pipeline archive loaded:"
MISMATCH = "identity does not match"
REJECTED = "Elisa shader manifest rejected"
COMPILE_MARKERS = ("shader compiled:", "shader compile FAILED")


def quote(path: Path) -> str:
    return str(path).replace("\\", "\\\\").replace('"', '\\"')


def write_manifest(shaders: Path) -> bytes:
    content = (json.dumps(shader_manifest(shaders), indent=2, sort_keys=True) + "\n").encode("utf-8")
    (shaders / SHADER_MANIFEST_NAME).write_bytes(content)
    return content


def archive_key(manifest: bytes, build_identity: int) -> str:
    digest = hashlib.sha256(manifest).hexdigest()
    material = f"elisa-pipeline-archive-v1\n{digest}\nmetal\n{build_identity}"
    return hashlib.sha256(material.encode("ascii")).hexdigest()


def tree_digest(root: Path) -> str:
    hash_ = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        hash_.update(path.relative_to(root).as_posix().encode("utf-8") + b"\0")
        if path.is_file():
            hash_.update(path.read_bytes())
    return hash_.hexdigest()


class Stage:
    def __init__(self, base: Path, executable: Path, project: Path, library: Path, label: str):
        self.base = base
        self.app = base / "app"
        self.shaders = self.app / "shaders"
        (self.shaders / "metal").mkdir(parents=True)
        # A unique executable name per trial keeps Metal's per-process caches
        # from reusing another trial's state where the OS keys them by name.
        self.executable = self.app / f"elisa-maze-{label}-{os.getpid()}-{time.time_ns()}"
        shutil.copy2(executable, self.executable)
        for relative in (packaged_maze_smoke.BUNDLE_PATH, packaged_maze_smoke.TEXTURE_BUNDLE_PATH):
            (self.app / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(project / relative, self.app / relative)
        for binary in sorted(library.rglob("*.cso")):
            target = self.shaders / "metal" / binary.relative_to(library)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(binary, target)
        self.binaries = len(list((self.shaders / "metal").rglob("*.cso")))
        self.manifest = write_manifest(self.shaders)
        self.home = base / "home"
        self.tmp = base / "tmp"
        self.cache = base / "cache"
        for folder in (self.home, self.tmp, self.cache):
            folder.mkdir()
        self.archive = self.cache / "pipelines.archive"
        self.profile = base / "packaged-shaders.sb"
        self.profile.write_text("(version 1)\n(allow default)\n"
            f'(deny file-read* file-write* (subpath "{quote(ROOT.parent)}"))\n'
            f'(deny file-write* (subpath "{quote(self.shaders)}"))\n', encoding="utf-8")
        self.environment = packaged_maze_smoke.application_environment(project, self.app, self.shaders)
        for key in [key for key in self.environment if key.startswith(("WICKED_", "DYLD_"))]:
            del self.environment[key]
        self.environment.update({"ELISA_ENGINE_SHADER_MANIFEST": str(self.shaders / SHADER_MANIFEST_NAME),
            "HOME": str(self.home), "TMPDIR": str(self.tmp) + "/", "ELISA_FRAME_HITCH_REPORT": "1"})

    def run(self, archive_variable: str | None) -> tuple[int, str, float]:
        environment = dict(self.environment)
        if archive_variable is not None:
            environment[archive_variable] = str(self.archive)
        started = time.perf_counter()
        result = subprocess.run(["/usr/bin/sandbox-exec", "-f", str(self.profile), str(self.executable)],
            cwd=self.base, env=environment, capture_output=True, text=True, timeout=600, check=False)
        return result.returncode, result.stdout + result.stderr, (time.perf_counter() - started) * 1000


def hitches(output: str) -> dict[str, float]:
    match = HITCHES.findall(output)
    if not match:
        raise ValueError("launch did not report frame hitches")
    pipelines, first, later, frames = (int(value) for value in match[0])
    return {"pipelines_ms": pipelines / 1000, "first_frame_ms": first / 1000,
        "later_max_ms": later / 1000, "frames": frames}


def require(condition: bool, message: str, output: str = "") -> None:
    if not condition:
        if output:
            print("\n".join(output.splitlines()[-40:]), file=sys.stderr)
        raise ValueError(message)


def trial(executable: Path, project: Path, library: Path, build_identity: int,
    label: str, invalidation: bool) -> dict[str, dict[str, float]]:
    with tempfile.TemporaryDirectory(prefix="Elisa packaged shaders ") as temporary:
        stage = Stage(Path(temporary).resolve(), executable, project, library, label)
        # Control: the sandbox really hides Wicked's shader sources.
        source_control = next(library.parent.glob("*.hlsl"))
        control = subprocess.run(["/usr/bin/sandbox-exec", "-f", str(stage.profile), "/bin/cat",
            str(source_control)], capture_output=True, timeout=10, check=False)
        require(control.returncode != 0, f"sandbox allowed reading source shader {source_control}")
        before = tree_digest(stage.shaders)

        status, output, cold_ms = stage.run("WICKED_METAL_PIPELINE_ARCHIVE_CAPTURE")
        require(status == 0, f"cold packaged launch exited {status}", output)
        require(not any(marker in output for marker in COMPILE_MARKERS),
            "cold packaged launch compiled a shader", output)
        require(tree_digest(stage.shaders) == before, "cold launch changed the packaged shaders")
        identity = stage.archive.with_name(stage.archive.name + ".elisa-identity")
        require(stage.archive.is_file() and identity.is_file(), "cold launch published no keyed archive", output)
        expected = archive_key(stage.manifest, build_identity)
        require(f"shader={expected}" in identity.read_text(encoding="utf-8"),
            "archive identity is not keyed by manifest, backend and engine build")
        cold = hitches(output) | {"launch_ms": cold_ms}

        status, output, warm_ms = stage.run("WICKED_METAL_PIPELINE_ARCHIVE")
        require(status == 0 and LOADED in output, f"warm packaged launch exited {status} without the archive", output)
        require(tree_digest(stage.shaders) == before, "warm launch changed the packaged shaders")
        warm = hitches(output) | {"launch_ms": warm_ms}
        print(f"Packaged shaders ({stage.binaries} Metal binaries, sources denied): "
            f"cold {cold}, warm {warm}", flush=True)
        if invalidation:
            invalidate(stage)
        return {"cold": cold, "warm": warm}


def invalidate(stage: Stage) -> None:
    metal = stage.shaders / "metal"
    # A changed shader package (one more permutation) changes the manifest
    # bytes, so the archive key changes and the old capture is rejected.
    first = sorted(metal.glob("*.cso"))[0]
    variant = metal / "elisa_r13_variantCS.cso"
    shutil.copy2(first, variant)
    original_manifest = stage.manifest
    write_manifest(stage.shaders)
    status, output, _ = stage.run("WICKED_METAL_PIPELINE_ARCHIVE")
    require(status == 0 and MISMATCH in output and LOADED not in output,
        "a changed shader package did not invalidate the pipeline archive", output)
    variant.unlink()
    (stage.shaders / SHADER_MANIFEST_NAME).write_bytes(original_manifest)
    print("ok: a changed shader package rejected the old pipeline archive", flush=True)

    # Tampering one packaged binary in place fails manifest verification before
    # Wicked starts: no fallback to sources, no archive use.
    original = first.read_bytes()
    tampered = bytearray(original)
    tampered[len(tampered) // 2] ^= 0xFF
    first.write_bytes(bytes(tampered))
    status, output, _ = stage.run("WICKED_METAL_PIPELINE_ARCHIVE")
    require(status != 0 and REJECTED in output and LOADED not in output,
        f"a tampered packaged shader was not rejected (exit {status})", output)
    first.write_bytes(original)
    print(f"ok: a tampered packaged shader stopped startup (exit {status})", flush=True)

    status, output, _ = stage.run("WICKED_METAL_PIPELINE_ARCHIVE")
    require(status == 0 and LOADED in output, "the restored package did not load its archive again", output)
    print("ok: the restored package loaded its archive again", flush=True)


def build_identity_of(executable: Path) -> int:
    provenance = executable.with_name(executable.name + ".provenance.json")
    return int(json.loads(provenance.read_text(encoding="utf-8"))["build_identity"], 16)


def run(executable: Path, project: Path, library: Path, trials: int = 1) -> int:
    if sys.platform != "darwin" or not Path("/usr/bin/sandbox-exec").is_file():
        print("packaged shader smoke requires macOS sandbox-exec", file=sys.stderr)
        return 2
    try:
        require(len(list(library.glob("*.cso"))) > 0, f"no packaged Metal binaries in {library}")
        build_identity = build_identity_of(executable)
        for index in range(trials):
            trial(executable, project, library, build_identity, f"t{index + 1}", index == 0)
    except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
        print(f"packaged shader smoke failed: {error}", file=sys.stderr)
        return 1
    return 0


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("project", type=Path)
    parser.add_argument("metal_library", type=Path, help="compiled Metal .cso directory to package")
    parser.add_argument("--trials", type=int, default=1)
    args = parser.parse_args(arguments)
    if not 1 <= args.trials <= 10:
        parser.error("trials must be 1-10")
    return run(args.executable.resolve(strict=True), args.project.resolve(strict=True),
        args.metal_library.resolve(strict=True), args.trials)


if __name__ == "__main__":
    raise SystemExit(main())
