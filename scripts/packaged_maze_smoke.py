#!/usr/bin/env python3
"""Run the native maze from a staged directory outside the engine checkout.

A03 requires runtime loading to work outside the checkout. This stages the
built maze executable and its cooked ELPK bundle, and nothing else, in a
temporary directory. It then runs the executable under a macOS sandbox profile
that denies every read and write inside the checkout. The same sandbox must
turn a missing, escaping, or corrupted bundle into the maze's asset
registration failure (exit 17) rather than a crash or a silent fallback.
"""

from __future__ import annotations

import json
import os
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_PATH = Path("assets/maze_tile.elpk")
ASSET_REGISTRATION_FAILED = 17
HEADER_BYTES = 32
ENTRY_BYTES = 48


def sandbox_profile(checkout: Path) -> str:
    escaped = str(checkout).replace("\\", "\\\\").replace('"', '\\"')
    return (
        "(version 1)\n"
        "(allow default)\n"
        f'(deny file-read* file-write* (subpath "{escaped}"))\n'
    )


def section_span(bundle: bytes, name: str) -> tuple[int, int]:
    magic, _version, count, index_offset, _index_size, _reserved = struct.unpack_from("<4sHHQQQ", bundle, 0)
    if magic != b"ELPK" or index_offset != HEADER_BYTES:
        raise ValueError("staged bundle is not an ELPK version-1 package")
    for index in range(count):
        raw_name, offset, stored, _unpacked, _compression, _crc = struct.unpack_from(
            "<16sQQQII", bundle, HEADER_BYTES + index * ENTRY_BYTES)
        if raw_name.split(b"\0", 1)[0].decode("ascii") == name:
            return offset, stored
    raise ValueError(f"staged bundle has no {name!r} section")


def application_environment(project: Path, project_root: Path, shader_path: Path) -> dict[str, str]:
    environment = {key: value for key, value in os.environ.items() if not key.startswith("ELISA_")}
    config = json.loads((project / "elisa.project.json").read_text(encoding="utf-8"))
    application = config.get("application", {})
    environment["ELISA_PROJECT_TITLE"] = str(application.get("title", "Elisa Engine"))
    environment["ELISA_PROJECT_WIDTH"] = str(application.get("width", 960))
    environment["ELISA_PROJECT_HEIGHT"] = str(application.get("height", 720))
    environment["ELISA_PROJECT_HIDDEN"] = "1" if application.get("hidden", False) else "0"
    environment["ELISA_PROJECT_ROOT"] = str(project_root)
    environment["ELISA_ENGINE_SHADER_PATH"] = str(shader_path)
    return environment


def run_sandboxed(profile: Path, executable: Path, working_directory: Path,
    environment: dict[str, str]) -> int:
    command = ["/usr/bin/sandbox-exec", "-f", str(profile), str(executable)]
    print("+", shlex.join(command), flush=True)
    return subprocess.run(command, cwd=working_directory, env=environment, check=False).returncode


def check(label: str, status: int, expected: int) -> bool:
    passed = status == expected
    print(f"{'ok' if passed else 'FAILED'}: {label} (exit {status}, expected {expected})", flush=True)
    return passed


def run(executable: Path, project: Path, shader_path: Path, checkout: Path = ROOT) -> int:
    if sys.platform != "darwin" or not Path("/usr/bin/sandbox-exec").is_file():
        print("packaged maze smoke requires macOS sandbox-exec", file=sys.stderr)
        return 2
    checkout = checkout.resolve(strict=True)
    bundle = (project / BUNDLE_PATH).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="Elisa packaged maze ") as temporary:
        base = Path(temporary).resolve()
        if base == checkout or checkout in base.parents:
            print("staging directory must be outside the checkout", file=sys.stderr)
            return 2
        profile = base / "deny-checkout.sb"
        profile.write_text(sandbox_profile(checkout), encoding="utf-8")
        working_directory = base / "elsewhere"
        working_directory.mkdir()
        stage = base / "elisa-maze"
        staged_executable = stage / executable.name
        staged_bundle = stage / BUNDLE_PATH
        staged_bundle.parent.mkdir(parents=True)
        shutil.copy2(executable, staged_executable)
        shutil.copy2(bundle, staged_bundle)

        environment = application_environment(project, stage, shader_path)
        results = [check("staged maze runs with the checkout denied",
            run_sandboxed(profile, staged_executable, working_directory, environment), 0)]

        # Control: the sandbox really denies the checkout's own copy of the bundle.
        checkout_root = application_environment(project, project.resolve(), shader_path)
        results.append(check("the checkout's bundle is unreadable inside the sandbox",
            run_sandboxed(profile, staged_executable, working_directory, checkout_root),
            ASSET_REGISTRATION_FAILED))

        original = staged_bundle.read_bytes()
        staged_bundle.unlink()
        results.append(check("a missing bundle fails registration",
            run_sandboxed(profile, staged_executable, working_directory, environment),
            ASSET_REGISTRATION_FAILED))

        # A readable bundle outside the project root must not be reachable by symlink.
        outside = base / "outside" / BUNDLE_PATH.name
        outside.parent.mkdir()
        outside.write_bytes(original)
        staged_bundle.symlink_to(outside)
        results.append(check("a bundle symlink escaping the project root is rejected",
            run_sandboxed(profile, staged_executable, working_directory, environment),
            ASSET_REGISTRATION_FAILED))
        staged_bundle.unlink()

        for section in ("mesh", "wallalbedo"):
            corrupted = bytearray(original)
            offset, stored = section_span(original, section)
            corrupted[offset + stored // 2] ^= 0xFF
            staged_bundle.write_bytes(bytes(corrupted))
            results.append(check(f"a corrupted {section} section is rejected",
                run_sandboxed(profile, staged_executable, working_directory, environment),
                ASSET_REGISTRATION_FAILED))

        staged_bundle.write_bytes(original)
        results.append(check("the restored bundle runs again",
            run_sandboxed(profile, staged_executable, working_directory, environment), 0))
    return 0 if all(results) else 1


def main(arguments: list[str]) -> int:
    if len(arguments) != 3:
        print("usage: packaged_maze_smoke.py EXECUTABLE PROJECT SHADER_PATH", file=sys.stderr)
        return 2
    return run(Path(arguments[0]).resolve(strict=True), Path(arguments[1]).resolve(strict=True),
        Path(arguments[2]).resolve(strict=True))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
