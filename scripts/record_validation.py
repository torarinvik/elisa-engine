"""Validate proof outcomes and atomically record the exact inputs to a passing check."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Optional


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_state(path: Path) -> Optional[dict]:
    root = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, check=False,
    )
    if root.returncode != 0:
        return None
    repository = Path(root.stdout.strip())
    commit = subprocess.run(
        ["git", "-C", str(repository), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()
    status = subprocess.run(
        ["git", "-C", str(repository), "status", "--porcelain", "--untracked-files=all"],
        capture_output=True, text=True, check=True,
    ).stdout
    return {"commit": commit, "dirty": bool(status)}


def source_manifest(root: Path) -> dict:
    paths = [root / name for name in ("README.md", "dependencies.md", "Elisa_Engine_Architecture_and_Plan.md")]
    for directory in ("src", "test", "proof", "scripts", "docs", "examples"):
        paths.extend(path for path in (root / directory).rglob("*") if path.is_file())
    files = {}
    digest = hashlib.sha256()
    for path in sorted(set(paths)):
        if not path.is_file() or "__pycache__" in path.parts or path.suffix == ".pyc":
            continue
        relative = path.relative_to(root).as_posix()
        file_hash = sha256_file(path)
        files[relative] = file_hash
        digest.update(relative.encode("utf-8") + b"\0" + file_hash.encode("ascii") + b"\n")
    return {"sha256": digest.hexdigest(), "files": files}


def tool_identity(path_text: str) -> dict:
    resolved = shutil.which(path_text) if "/" not in path_text else path_text
    if resolved is None:
        raise ValueError(f"tool was not found: {path_text}")
    path = Path(resolved).expanduser().resolve(strict=True)
    if not path.is_file():
        raise ValueError(f"tool is not a file: {path}")
    return {"path": str(path), "sha256": sha256_file(path), "git": git_state(path.parent)}


def verified_proof(path: Path) -> dict:
    result = json.loads(path.read_text(encoding="utf-8"))
    summary = result.get("summary", {})
    replay = result.get("replay", {})
    kernel = result.get("kernel", {})
    count = summary.get("obligations")
    if not (
        result.get("status") == "proved"
        and result.get("verification_state") == "proved"
        and isinstance(count, int) and count > 0
        and summary.get("proven") == count
        and summary.get("failed") == 0
        and summary.get("semantic_diagnostics") == 0
        and summary.get("semantic_errors") == 0
        and replay.get("replayed") == count
        and replay.get("gaps") == 0
        and kernel.get("independent_replay") is True
    ):
        raise ValueError(f"proof outcome is not fully proved and replayed: {path}")
    return {"sha256": sha256_file(path), "obligations": count, "replayed": replay["replayed"]}


def scene_manifest_matches_bridge(root: Path) -> dict:
    # The only machine-checked link between the static host fixture and the
    # Elisa canonical scene: these constants must equal
    # SceneBridge::canonical_fixture_epoch/object/camera, manifest_version,
    # and the default viewport. Positions stay scenario data and are not
    # pinned here. Drift fails the gate instead of letting the two probes
    # and the Elisa bridge tests silently diverge.
    expected = {
        "version": "1",
        "epoch": "7",
        "entity": "3",
        "camera_entity": "4",
        "viewport_width": "320",
        "viewport_height": "200",
        "commands": "create,update,destroy",
    }
    manifest = root / "backends/scene_manifest.txt"
    values = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text.startswith("#") or "=" not in text:
            continue
        key, value = text.split("=", 1)
        values[key.strip()] = value.strip()
    for key, want in expected.items():
        if values.get(key) != want:
            raise ValueError(f"scene manifest drift: {key}={values.get(key)!r}, expected {want!r}")
    # The wall list is the Elisa maze topology carried to the native host.
    # 34 is pinned independently by test/maze.elisa (maze_wall_count) and
    # test/maze.elisa also checks the layout list against Maze::is_wall for
    # every cell, so this only catches fixture drift, not engine drift.
    wall_spec = values.get("walls", "")
    wall_cells = [entry for entry in wall_spec.split(";") if entry]
    if len(wall_cells) != 34:
        raise ValueError(f"scene manifest wall count drift: {len(wall_cells)}, expected 34")
    for entry in wall_cells:
        parts = entry.split(",")
        if len(parts) != 2 or not all(part.lstrip("-").isdigit() for part in parts):
            raise ValueError(f"scene manifest wall cell malformed: {entry!r}")
    return {"sha256": sha256_file(manifest), "fields": len(values), "wall_cells": len(wall_cells)}


def main(arguments: list[str]) -> int:
    if len(arguments) != 4:
        print("usage: record_validation.py ENGINE_ROOT COMPILER PROVER ELISASCRIPT", file=sys.stderr)
        return 2
    root, compiler, prover, launcher = arguments
    engine = Path(root).resolve(strict=True)
    report_path = engine / "build/validation.json"
    try:
        proofs = {
            "entity_id": verified_proof(engine / "build/entity-id-proof.json"),
            "world": verified_proof(engine / "build/world-proof.json"),
        }
        compiler_path = Path(compiler).resolve(strict=True)
        compiler_product = compiler_path.parent.parent / "bin/elisac-stage1" if compiler_path.name == "elisac_stage1.sh" else compiler_path
        report = {
            "status": "passed",
            "engine": {"git": git_state(engine), "source_manifest": source_manifest(engine)},
            "tools": {
                "compiler_entry": tool_identity(compiler),
                "compiler_product": tool_identity(str(compiler_product)),
                "prover": tool_identity(prover),
                "elisascript": tool_identity(launcher),
            },
            "proofs": proofs,
            "scene_manifest": scene_manifest_matches_bridge(engine),
            "checks": ["identity", "world", "geometry", "assets", "input", "backend_capabilities", "sdl3_platform", "godot_host", "fake_bridge", "ffi_contracts", "recording", "clock", "headless_game", "scene_bridge", "image_compare", "asset_cooking", "maze_slice", "maze_game", "anim_state", "grid_nav", "inspector_perf", "replication_scope", "physics_authority", "runtime_scheduler", "editor_reload", "net_session", "maze_bundle", "audio_ownership", "anim_codec", "asset_catalogue", "scene_manifest_link", "affine_copy_rejections"],
        }
        temporary = report_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        os.replace(temporary, report_path)
    except (OSError, ValueError, json.JSONDecodeError, subprocess.CalledProcessError) as failure:
        print(f"Validation report failed: {failure}", file=sys.stderr)
        return 1
    print("Validation report written.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
