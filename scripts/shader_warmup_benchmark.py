#!/usr/bin/env python3
"""Measure Wicked startup with cold shaders, cached shaders, and a Metal archive."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from package_macos_app import PackageError, shader_manifest
from shader_library_publish import publish
from shader_permutation_selection import select_binaries


ENGINE_ROOT = Path(__file__).resolve().parents[1]
WARMUP_RESULT = re.compile(
    r"shader warm-up: frames=(\d+) first_frame_us=(\d+) "
    r"warm_median_us=(\d+) warm_p95_us=(\d+)"
)


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wicked-root", type=Path,
        default=Path(os.environ.get("WICKED_ROOT", ENGINE_ROOT.parent / "WickedEngine")),
        help="WickedEngine checkout (or WICKED_ROOT)")
    parser.add_argument("--probe", type=Path,
        default=ENGINE_ROOT / "build" / "wicked-native-probe",
        help="already-built native Wicked probe")
    parser.add_argument("--selection-output", type=Path,
        help="write observed Metal permutation names after all runtime checks pass")
    parser.add_argument("--offline-shaders", type=Path,
        help="Metal binary directory to verify against the observed runtime selection")
    return parser.parse_args(argv)


def run_probe(probe: Path, wicked_source: Path, manifest: Path,
    shader_root: Path, pipeline_archive: Path | None = None,
    capture_pipeline_archive: bool = False) -> tuple[int, str, int]:
    environment = dict(os.environ)
    environment["ELISA_PROBE_SHADER_ROOT"] = str(shader_root)
    environment["ELISA_SHADER_WARMUP_PROBE"] = "1"
    environment.pop("WICKED_METAL_PIPELINE_ARCHIVE", None)
    environment.pop("WICKED_METAL_PIPELINE_ARCHIVE_CAPTURE", None)
    if pipeline_archive is not None:
        variable = ("WICKED_METAL_PIPELINE_ARCHIVE_CAPTURE" if capture_pipeline_archive
            else "WICKED_METAL_PIPELINE_ARCHIVE")
        environment[variable] = str(pipeline_archive)
    screenshot = shader_root.parent / "shader-warmup.png"
    command = [str(probe), str(wicked_source), str(manifest), str(screenshot), "alwaysactive"]
    started = time.perf_counter_ns()
    result = subprocess.run(command, cwd=ENGINE_ROOT, env=environment,
        capture_output=True, text=True, check=False)
    elapsed_ms = (time.perf_counter_ns() - started) // 1_000_000
    output = result.stdout + result.stderr
    if result.returncode != 0:
        lines = output.splitlines()
        print("\n".join(lines[-60:]), file=sys.stderr)
        raise RuntimeError(f"Wicked warm-up probe exited with status {result.returncode}")
    return elapsed_ms, output, len(list(shader_root.rglob("*.cso")))


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    if sys.platform != "darwin":
        print("The Wicked shader warm-up benchmark currently requires macOS/Metal.", file=sys.stderr)
        return 2
    wicked_root = args.wicked_root.expanduser().resolve()
    wicked_source = wicked_root / "WickedEngine"
    probe = args.probe.expanduser().resolve()
    manifest = ENGINE_ROOT / "backends" / "scene_manifest.txt"
    if not probe.is_file() or not os.access(probe, os.X_OK):
        print(f"Native Wicked probe is missing: {probe}", file=sys.stderr)
        print("Build it with: elisascript scripts/wicked_probe.elisascript build", file=sys.stderr)
        return 2
    if not (wicked_source / "shaders").is_dir() or not manifest.is_file():
        print("Wicked shader sources or the shared scene manifest are missing.", file=sys.stderr)
        return 2

    try:
        with tempfile.TemporaryDirectory(prefix="elisa-shader-warmup-") as temporary:
            shader_root = Path(temporary) / "shaders"
            (shader_root / "metal").mkdir(parents=True)
            pipeline_archive = Path(temporary) / "metal-pipelines.archive"
            # Root preflight is intentionally bypassed by this native-only probe.
            # The marker makes the output tree structurally valid while leaving
            # every shader requested by Wicked absent for the cold launch.
            (shader_root / "metal" / "elisa-warmup-anchor.cso").write_bytes(b"anchor")

            cold_ms, cold_output, cold_count = run_probe(
                probe, wicked_source, manifest, shader_root, pipeline_archive,
                capture_pipeline_archive=True)
            cold_result = WARMUP_RESULT.search(cold_output)
            if cold_result is None:
                raise RuntimeError("cold run did not report its first and warmed frame timings")
            cold_compiled = cold_count - 1
            if cold_compiled <= 0:
                raise RuntimeError("cold run did not create any compiled shader binaries")

            warm_ms, warm_output, warm_count = run_probe(
                probe, wicked_source, manifest, shader_root)
            warm_result = WARMUP_RESULT.search(warm_output)
            if warm_result is None:
                raise RuntimeError("second run did not report its first and warmed frame timings")
            warm_new = warm_count - cold_count
            if warm_new != 0:
                raise RuntimeError(f"second launch compiled {warm_new} additional shader binaries")

            if "Metal pipeline archive captured:" not in cold_output:
                raise RuntimeError("cold run did not publish its Metal pipeline archive")
            if not pipeline_archive.is_file() or pipeline_archive.stat().st_size == 0:
                raise RuntimeError("cold run did not produce a readable Metal pipeline archive")

            archive_ms, archive_output, archive_count = run_probe(
                probe, wicked_source, manifest, shader_root, pipeline_archive)
            archive_result = WARMUP_RESULT.search(archive_output)
            if archive_result is None:
                raise RuntimeError("archive-backed run did not report its first and warmed frame timings")
            if "Metal pipeline archive loaded:" not in archive_output:
                raise RuntimeError("archive-backed run did not load the captured Metal pipeline archive")
            archive_new = archive_count - warm_count
            if archive_new != 0:
                raise RuntimeError(f"archive-backed launch compiled {archive_new} additional shader binaries")

            selection = sorted(path.relative_to(shader_root / "metal").as_posix()
                for path in (shader_root / "metal").rglob("*.cso")
                if path.name != "elisa-warmup-anchor.cso")
            if args.offline_shaders is not None:
                offline = args.offline_shaders.expanduser().resolve()
                reduced_root = Path(temporary) / "reduced-shaders"
                binaries = select_binaries(offline, list(offline.rglob("*.cso")), set(selection))
                publish(reduced_root, offline, binaries)
                before = shader_manifest(reduced_root)
                reduced_ms, reduced_output, reduced_count = run_probe(
                    probe, wicked_source, manifest, reduced_root)
                if WARMUP_RESULT.search(reduced_output) is None:
                    raise RuntimeError("reduced offline run did not report frame timings")
                if shader_manifest(reduced_root) != before:
                    raise RuntimeError("reduced offline run added or changed shader binaries")
                print(f"Reduced offline set: {reduced_count} unchanged binaries; launch {reduced_ms} ms")

            if args.selection_output is not None:
                destination = args.selection_output.expanduser().resolve()
                temporary_output = None
                try:
                    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8",
                            dir=destination.parent, prefix=".elisa-selection-", delete=False) as stream:
                        temporary_output = Path(stream.name)
                        stream.write(json.dumps(selection, indent=2) + "\n")
                    os.replace(temporary_output, destination)
                finally:
                    if temporary_output is not None:
                        temporary_output.unlink(missing_ok=True)
                print(f"Observed {len(selection)} Metal permutations: {destination}")

            print(f"Cold launch: {cold_ms} ms; compiled {cold_compiled} shader binaries")
            print(f"  first frame: {cold_result[2]} us; later median/p95: "
                f"{cold_result[3]}/{cold_result[4]} us")
            print(f"Shader-cache launch: {warm_ms} ms; new shader binaries: {warm_new}")
            print(f"  first frame: {warm_result[2]} us; later median/p95: "
                f"{warm_result[3]}/{warm_result[4]} us")
            print(f"Pipeline-archive launch: {archive_ms} ms; new shader binaries: {archive_new}")
            print(f"  first frame: {archive_result[2]} us; later median/p95: "
                f"{archive_result[3]}/{archive_result[4]} us")
            print(f"  Metal archive size: {pipeline_archive.stat().st_size} bytes")
    except (OSError, RuntimeError, PackageError) as error:
        print(f"shader warm-up benchmark: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
