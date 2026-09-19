"""Validate proof outcomes and atomically record the exact inputs to a passing check."""

import hashlib
import math
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Optional

from record_validation_assets import (
    asset_catalogue_database,
    asset_import_self_test,
    cooked_texture,
    cooked_texture_bc1,
    cooked_texture_packed,
    sha256_file,
)


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
    # The entity host object must sit where the Elisa scripted run left the
    # player. 1,4 is the mid-game snapshot pinned by test/maze.elisa via
    # maze_fixture_state (three moves, not yet won), and 0.6/-2.1 is the
    # shared world mapping the hosts and scripts/compare_renders.py both use.
    if values.get("player_final") != "1,4":
        raise ValueError(f"scene manifest player_final drift: {values.get('player_final')!r}")
    (player_x, player_y) = (int(part) for part in values["player_final"].split(","))
    if not (0 <= player_x < 8 and 0 <= player_y < 8):
        raise ValueError(f"scene manifest player_final out of grid: {values['player_final']!r}")
    final_x = player_x * 0.6 - 2.1
    final_y = player_y * 0.6 - 2.1
    for key, expected in (("object_x", final_x), ("object_y", final_y)):
        if abs(float(values.get(key, "nan")) - expected) > 1e-6:
            raise ValueError(f"scene manifest {key} drift: {values.get(key)!r}, expected {expected}")
    # Declared occluded cells must be in-grid and open (the entity stands on
    # an open cell); otherwise the topology check would hide real drift.
    wall_set = set()
    for entry in wall_cells:
        (x, y) = entry.split(",")
        wall_set.add((int(x), int(y)))
    # Optional: cells the fixture declares as hidden behind the foreground
    # object. The checker skips exactly these, so they must be real open
    # cells; an empty list just means nothing is occluded.
    occluded = [entry for entry in values.get("occluded", "").split(";") if entry]
    for entry in occluded:
        parts = entry.split(",")
        if len(parts) != 2:
            raise ValueError(f"scene manifest occluded cell malformed: {entry!r}")
        (x, y) = int(parts[0]), int(parts[1])
        if not (0 <= x < 8 and 0 <= y < 8):
            raise ValueError(f"scene manifest occluded cell out of grid: {entry!r}")
        if (x, y) in wall_set:
            raise ValueError(f"scene manifest occluded cell is a wall: {entry!r}")
    # Game markers must match the MazeGame rules. The literals here mirror the
    # ones test/maze.elisa pins via is_key/is_door/is_hazard and the goal
    # accessors; the Elisa test is the source of truth and this catches
    # fixture drift.
    def cell_of(key, expected):
        value = values.get(key, "")
        if value != expected:
            raise ValueError(f"scene manifest {key} drift: {value!r}, expected {expected!r}")
        (x, y) = (int(part) for part in value.split(","))
        if not (0 <= x < 8 and 0 <= y < 8):
            raise ValueError(f"scene manifest {key} out of grid: {value!r}")
        if (x, y) in wall_set:
            raise ValueError(f"scene manifest {key} is a wall: {value!r}")
        return (x, y)

    goal = cell_of("goal", "6,6")
    key_cell = cell_of("key", "2,6")
    door = cell_of("door", "5,6")
    hazard_cells = []
    for entry in [part for part in values.get("hazards", "").split(";") if part]:
        parts = entry.split(",")
        if len(parts) != 2:
            raise ValueError(f"scene manifest hazard cell malformed: {entry!r}")
        (x, y) = int(parts[0]), int(parts[1])
        if not (0 <= x < 8 and 0 <= y < 8):
            raise ValueError(f"scene manifest hazard out of grid: {entry!r}")
        if (x, y) in wall_set:
            raise ValueError(f"scene manifest hazard is a wall: {entry!r}")
        hazard_cells.append((x, y))
    if len(hazard_cells) != 2:
        raise ValueError(f"scene manifest hazard count drift: {len(hazard_cells)}, expected 2")
    try:
        budget = int(values.get("frame_budget_us", ""))
    except ValueError:
        raise ValueError(f"scene manifest frame_budget_us is not an integer: {values.get('frame_budget_us')!r}")
    if budget <= 0 or budget > 16667:
        raise ValueError(f"scene manifest frame_budget_us out of range: {budget}")
    hunter = cell_of("hunter", "1,1")
    route_cells = [entry for entry in values.get("hunter_route", "").split(";") if entry]
    if len(route_cells) != 4:
        raise ValueError(f"scene manifest hunter_route count drift: {len(route_cells)}, expected 4")
    route_points = []
    for entry in route_cells:
        (rx, ry) = (int(part) for part in entry.split(","))
        if not (0 <= rx < 8 and 0 <= ry < 8) or (rx, ry) in wall_set:
            raise ValueError(f"scene manifest hunter_route cell invalid: {entry!r}")
        if route_points:
            (px, py) = route_points[-1]
            if abs(px - rx) + abs(py - ry) != 1:
                raise ValueError(f"scene manifest hunter_route is not contiguous: {entry!r}")
        route_points.append((rx, ry))
    if route_points[0] != (1, 1) or route_points[-1] != (4, 1):
        raise ValueError("scene manifest hunter_route endpoints drift")
    if route_points[0] != (1, 1):
        raise ValueError("scene manifest hunter_route does not start at the spawn")
    try:
        fog_radius = int(values.get("fog_radius", ""))
    except ValueError:
        raise ValueError(f"scene manifest fog_radius is not an integer: {values.get('fog_radius')!r}")
    if fog_radius <= 0 or fog_radius > 8:
        raise ValueError(f"scene manifest fog_radius out of range: {fog_radius}")
    asset_rel = values.get("mesh_asset", "")
    if not asset_rel or not (root / asset_rel).is_file():
        raise ValueError(f"scene manifest mesh_asset is missing: {asset_rel!r}")
    try:
        mesh_triangles = int(values.get("mesh_triangles", ""))
    except ValueError:
        raise ValueError(f"scene manifest mesh_triangles is not an integer: {values.get('mesh_triangles')!r}")
    if mesh_triangles <= 0:
        raise ValueError(f"scene manifest mesh_triangles out of range: {mesh_triangles}")
    # Menu state for the host UI: the action count, the enabled mask, and the
    # focused row must be mutually consistent and the focus row must be enabled.
    if "menu_actions" in values or "menu_enabled" in values:
        try:
            menu_actions = int(values.get("menu_actions", ""))
            menu_focus = int(values.get("menu_focus", ""))
        except ValueError:
            raise ValueError(f"scene manifest menu numbers malformed: {values.get('menu_actions')!r}, {values.get('menu_focus')!r}")
        enabled = [part for part in values.get("menu_enabled", "").split(",") if part]
        if menu_actions <= 0 or menu_actions > 8:
            raise ValueError(f"scene manifest menu_actions out of range: {menu_actions}")
        if len(enabled) != menu_actions or not all(part in ("0", "1") for part in enabled):
            raise ValueError(f"scene manifest menu_enabled does not match {menu_actions} actions: {enabled!r}")
        if menu_focus < 0 or menu_focus >= menu_actions or enabled[menu_focus] != "1":
            raise ValueError(f"scene manifest menu_focus {menu_focus} is not an enabled row")
        if "1" not in enabled:
            raise ValueError("scene manifest menu has no enabled action")
        if "menu_visible" in values:
            menu_visible = int(values["menu_visible"])
            if menu_visible <= 0 or menu_visible > menu_actions:
                raise ValueError(f"scene manifest menu_visible out of range: {menu_visible}")
            # The fixture's window starts at row 0, so the focused row must be
            # inside it.
            if menu_focus >= menu_visible:
                raise ValueError(f"scene manifest menu_focus {menu_focus} is outside the {menu_visible}-row window")
        if "menu_row_height" in values:
            row_height = int(values["menu_row_height"])
            if row_height <= 0 or row_height > 200:
                raise ValueError(f"scene manifest menu_row_height out of range: {row_height}")
        # Engine theme values: four RGBA channels in range, then the insets the
        # host adds around a row's label.
        for key in ("menu_style_background", "menu_style_text", "menu_style_disabled", "menu_style_focus"):
            if key in values:
                channels = values[key].split(",")
                if len(channels) != 4 or any(not c.isdigit() or int(c) > 255 for c in channels):
                    raise ValueError(f"scene manifest {key} is not an RGBA color: {values[key]!r}")
        if "menu_style_padding" in values:
            padding = int(values["menu_style_padding"])
            if padding < 0 or padding > 64:
                raise ValueError(f"scene manifest menu_style_padding out of range: {padding}")
        if "menu_style_border" in values:
            border = int(values["menu_style_border"])
            if border < 0 or border > 16:
                raise ValueError(f"scene manifest menu_style_border out of range: {border}")
    # Portable input mapping names, kept to the engine's button vocabulary so a
    # host never receives an enum ordinal.
    input_buttons = ("KeyA", "KeyD", "KeyW", "PadLeft", "PadRight", "PadUp")
    for key in ("input_forward", "input_left", "input_right"):
        if key in values and values[key] not in input_buttons:
            raise ValueError(f"scene manifest {key} is not a portable button: {values[key]!r}")
    # Skinned quad: four coordinate pairs whose corners form a rectangle (the
    # three consecutive edge vectors are axis-aligned and equal in length).
    if "skin_quad" in values:
        entries = [entry for entry in values["skin_quad"].split(";") if entry]
        if len(entries) != 4:
            raise ValueError(f"scene manifest skin_quad needs four vertices: {len(entries)}")
        points = []
        for entry in entries:
            parts = entry.split(",")
            if len(parts) != 2:
                raise ValueError(f"scene manifest skin_quad vertex malformed: {entry!r}")
            points.append((float(parts[0]), float(parts[1])))
        width = math.hypot(points[1][0] - points[0][0], points[1][1] - points[0][1])
        height = math.hypot(points[2][0] - points[1][0], points[2][1] - points[1][1])
        closing = math.hypot(points[0][0] - points[3][0], points[0][1] - points[3][1])
        if abs(width - 0.2) > 0.01 or abs(height - 0.2) > 0.01 or abs(closing - height) > 0.01:
            raise ValueError(f"scene manifest skin_quad is not a 0.2 square: {width:.3f}, {height:.3f}, {closing:.3f}")
    # Character leg pose: two segments of the bone lengths, with the knee bent
    # between the hip and the foot.
    def pose_pair(key):
        value = values.get(key, "")
        parts = value.split(",")
        if len(parts) != 2:
            raise ValueError(f"scene manifest {key} is not a coordinate pair: {value!r}")
        try:
            return (float(parts[0]), float(parts[1]))
        except ValueError:
            raise ValueError(f"scene manifest {key} is not numeric: {value!r}")
    if "pose_hip" in values:
        hip = pose_pair("pose_hip")
        knee = pose_pair("pose_knee")
        foot = pose_pair("pose_foot")
        if not (hip[1] > knee[1] > foot[1]):
            raise ValueError("scene manifest pose knee is not between the hip and the foot")
        upper = math.hypot(knee[0] - hip[0], knee[1] - hip[1])
        lower = math.hypot(foot[0] - knee[0], foot[1] - knee[1])
        if abs(upper - 0.6) > 0.01 or abs(lower - 0.6) > 0.01:
            raise ValueError(f"scene manifest pose segments are not the bone lengths: {upper:.3f}, {lower:.3f}")
    allowed_status = ("menu", "playing", "paused", "won", "lost", "exited")
    if values.get("game_status") not in allowed_status:
        raise ValueError(f"scene manifest game_status drift: {values.get('game_status')!r}")
    status_cell = cell_of("status_cell", "6,1")
    try:
        audio_cues = int(values.get("audio_cues", ""))
    except ValueError:
        raise ValueError(f"scene manifest audio_cues is not an integer: {values.get('audio_cues')!r}")
    if audio_cues <= 0 or audio_cues > 64:
        raise ValueError(f"scene manifest audio_cues out of range: {audio_cues}")
    if hunter == (player_x, player_y):
        raise ValueError("scene manifest hunter overlaps the player")
    markers = [goal, key_cell, door, hunter, status_cell] + hazard_cells
    if len(set(markers)) != len(markers):
        raise ValueError("scene manifest markers overlap")
    # The snapshot must leave the player on an empty cell so every marker is
    # still visible in a captured frame, and no marker may sit on the player.
    if (player_x, player_y) in wall_set:
        raise ValueError("scene manifest player_final is a wall")
    if (player_x, player_y) in markers:
        raise ValueError("scene manifest player_final overlaps a marker")
    return {"sha256": sha256_file(manifest), "fields": len(values),
            "wall_cells": len(wall_cells), "occluded_cells": len(occluded),
            "markers": len(markers), "frame_budget_us": budget, "audio_cues": audio_cues, "fog_radius": fog_radius, "mesh_triangles": mesh_triangles}


def cook_asset(root: Path) -> dict:
    # Offline cooking runs as part of validation: the source asset is turned
    # into a versioned package with a content hash, and a mismatch between the
    # cooked counts and the fixture fails the whole run rather than producing a
    # package that disagrees with what the hosts verified.
    result = subprocess.run(
        [sys.executable, str(root / "scripts/cook_assets.py"), str(root)],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"asset cooking failed: {result.stderr.strip() or result.stdout.strip()}")
    packages = sorted((root / "build/cooked").glob("*.pkg"))
    if not packages:
        raise ValueError("asset cooking produced no package")
    package = packages[0]
    return {"package": package.name, "sha256": sha256_file(package), "bytes": package.stat().st_size}


MAX_SOURCE_LINES = 600
SOURCE_TREES = ("src", "test", "examples", "proof", "scripts", "native", "backends")
SOURCE_SUFFIXES = (".elisa", ".py", ".gd", ".cpp", ".h", ".mm", ".elisascript")


def source_length_policy(root: Path) -> dict:
    # The project keeps every source file within 600 lines so a subsystem stays
    # readable and reviewable. This is a hard gate, not an aspiration: drift
    # fails validation instead of being noticed later.
    largest = 0
    largest_file = ""
    count = 0
    for tree in SOURCE_TREES:
        for path in sorted((root / tree).rglob("*")):
            if not path.is_file() or "__pycache__" in path.parts or path.suffix not in SOURCE_SUFFIXES:
                continue
            count += 1
            lines = len(path.read_text(encoding="utf-8", errors="replace").splitlines())
            if lines > largest:
                largest = lines
                largest_file = path.relative_to(root).as_posix()
    if largest > MAX_SOURCE_LINES:
        raise ValueError(f"source file exceeds {MAX_SOURCE_LINES} lines: {largest_file} has {largest}")
    return {"max_lines": largest, "max_file": largest_file, "files": count, "limit": MAX_SOURCE_LINES}


def dependency_provenance(root: Path) -> dict:
    # Record what each pinned dependency actually is. Single-file dependencies
    # are hash-checked against the pin in fetch_dependencies.py and a mismatch
    # fails the gate; a dependency that is simply not fetched is recorded as
    # absent rather than treated as a failure, because the gate does not require
    # the native dependencies. Git-pinned dependencies record their commit.
    import importlib.util
    script = root / "scripts/fetch_dependencies.py"
    spec = importlib.util.spec_from_file_location("fetch_dependencies_for_validation", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    recorded = {}
    for name, (revision, _url, expected, relative) in module.PINNED.items():
        path = root / "dependencies" / relative
        if not path.is_file():
            recorded[name] = {"present": False, "revision": revision}
            continue
        digest = sha256_file(path)
        if expected is not None and digest != expected:
            raise ValueError(f"dependency {name} hash mismatch: {digest} != {expected}")
        recorded[name] = {"present": True, "revision": revision, "sha256": digest}
    for name in ("ozz", "recast", "tracy", "gns", "basisu", "sdl3"):
        path = root / "dependencies" / name
        if (path / ".git").is_dir():
            commit = subprocess.run(
                ["git", "-C", str(path), "rev-parse", "HEAD"],
                capture_output=True, text=True, check=True,
            ).stdout.strip()
            recorded[name] = {"present": True, "commit": commit}
        else:
            recorded[name] = {"present": False}
    return {"pins": recorded, "present": sum(1 for entry in recorded.values() if entry["present"])}


def release_package(root: Path, compiler: str) -> dict:
    # A release is only recorded if the packager proved the archive
    # reproducible. The summary pins the archive hash and the platform, and
    # carries the platforms that are deliberately untested rather than
    # implying support from a dependency's own platform list.
    result = subprocess.run(
        [sys.executable, str(root / "scripts/package_release.py"), str(root), compiler],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise ValueError(f"release packaging failed: {result.stderr.strip() or result.stdout.strip()}")
    summary = json.loads((root / "build/release/release.json").read_text(encoding="utf-8"))
    if not summary.get("reproducible"):
        raise ValueError("release package did not prove reproducibility")
    return summary


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
        cooked = cook_asset(engine)
        release = release_package(engine, compiler)
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
            "source_length_policy": source_length_policy(engine),
            "cooked_asset": cooked,
            "cooked_texture": cooked_texture(engine),
            "cooked_texture_packed": cooked_texture_packed(engine),
            "cooked_texture_bc1": cooked_texture_bc1(engine),
            "asset_import_bounds": asset_import_self_test(engine),
            "asset_catalogue_database": asset_catalogue_database(engine),
            "dependencies": dependency_provenance(engine),
            "release": release,
            "checks": ["identity", "world", "geometry", "assets", "input", "backend_capabilities", "sdl3_platform", "godot_host", "fake_bridge", "ffi_contracts", "recording", "clock", "headless_game", "scene_bridge", "image_compare", "asset_cooking", "maze_slice", "maze_game", "anim_state", "grid_nav", "inspector_perf", "replication_scope", "physics_authority", "runtime_scheduler", "editor_reload", "net_session", "maze_bundle", "audio_ownership", "anim_codec", "asset_catalogue", "release_packaging", "asset_import_bounds", "cooked_texture", "cooked_texture_packed", "cooked_texture_bc1", "asset_catalogue_database", "dependency_pins", "source_length_policy", "scene_manifest_link", "affine_copy_rejections"],
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
