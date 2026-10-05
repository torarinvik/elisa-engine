#!/usr/bin/env python3
"""Build and run the Elisa-authored application lifecycle smoke project."""

from __future__ import annotations

import base64
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave
import zlib
import shutil
from pathlib import Path

import cook_physics_collision
from elisa_package import write_package
import native_smoke_artifacts


ROOT = Path(__file__).resolve().parents[1]
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_MINIMUM_LENGTH = 45
PNG_RGBA8_HEADER = bytes((8, 6, 0, 0, 0))
PNG_FILTER_NONE = 0
MIN_VISIBLE_CAPTURE_PIXELS = 1000
MIN_VISIBLE_CHANNEL_VALUE = 16
MIN_HIERARCHY_CAPTURE_PIXELS = 100
CHARACTER_COURSE_HUD_CAPTURE_HEIGHT = 96
MIN_CHARACTER_COURSE_HUD_CHANGED_PIXELS = 8
CAPTURE_PIXEL_CHANNEL_DELTA = 8


def native_smoke_environment(
    source: dict[str, str], *, allow_device_reopen: bool = False
) -> dict[str, str]:
    """Keep app smokes quiet regardless of user audio settings."""
    environment = dict(source)
    # The relaunch case normally validates that a saved output device can be
    # reopened. A caller can still force every app smoke onto the null route,
    # including that case, by setting this variable in the parent environment.
    requested_force = source.get("ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE", "")
    # Match default_device_forced_unavailable() in native/audio_service_abi.cpp:
    # any nonempty value not beginning with '0' forces the null route.
    force_device_unavailable = bool(requested_force) and not requested_force.startswith("0")
    environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"] = (
        "1" if force_device_unavailable or not allow_device_reopen else "0"
    )
    return environment


def decode_capture_png(data: bytes) -> tuple[int, int, bytes] | None:
    if len(data) < PNG_MINIMUM_LENGTH or data[:8] != PNG_SIGNATURE:
        return None
    offset = 8
    width = height = 0
    compressed = bytearray()
    saw_header = False
    saw_end = False
    while offset + 12 <= len(data):
        chunk_size = int.from_bytes(data[offset:offset + 4], "big")
        chunk_type = data[offset + 4:offset + 8]
        payload_start = offset + 8
        payload_end = payload_start + chunk_size
        chunk_end = payload_end + 4
        if chunk_end > len(data):
            return None
        payload = data[payload_start:payload_end]
        expected_crc = int.from_bytes(data[payload_end:chunk_end], "big")
        if zlib.crc32(chunk_type + payload) & 0xFFFFFFFF != expected_crc:
            return None
        if chunk_type == b"IHDR":
            if saw_header or len(payload) != 13:
                return None
            width = int.from_bytes(payload[0:4], "big")
            height = int.from_bytes(payload[4:8], "big")
            if payload[8:] != PNG_RGBA8_HEADER:
                return None
            saw_header = True
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            saw_end = chunk_size == 0
            offset = chunk_end
            break
        offset = chunk_end
    if not saw_header or not saw_end or offset != len(data) or width <= 0 or height <= 0:
        return None
    try:
        scanlines = zlib.decompress(compressed)
    except zlib.error:
        return None
    row_stride = width * 4 + 1
    if len(scanlines) != row_stride * height:
        return None
    pixels = bytearray(width * height * 4)
    for row in range(height):
        source_start = row * row_stride
        if scanlines[source_start] != PNG_FILTER_NONE:
            return None
        target_start = row * width * 4
        pixels[target_start:target_start + width * 4] = scanlines[source_start + 1:source_start + row_stride]
    return width, height, bytes(pixels)


def character_course_presentation_error(win_path: Path, fall_path: Path) -> str | None:
    """Require visible, same-sized outcome captures with a changed HUD region."""
    decoded = []
    for outcome, path in (("win", win_path), ("fall", fall_path)):
        if not path.is_file():
            return f"Character Course did not write its {outcome} presentation capture: {path}"
        image = decode_capture_png(path.read_bytes())
        if image is None:
            return f"Character Course {outcome} presentation is not a valid RGBA PNG: {path}"
        width, height, pixels = image
        visible_pixels = sum(1 for index in range(0, len(pixels), 4)
            if max(pixels[index:index + 3]) > MIN_VISIBLE_CHANNEL_VALUE)
        if width <= 0 or height <= 0 or visible_pixels < MIN_VISIBLE_CAPTURE_PIXELS:
            return f"Character Course {outcome} presentation is empty or has invalid dimensions."
        decoded.append(image)
    win, fall = decoded
    if win[:2] != fall[:2]:
        return "Character Course win and fall captures have mismatched dimensions."
    width, height = win[0], win[1]
    hud_height = min(height, CHARACTER_COURSE_HUD_CAPTURE_HEIGHT)
    changed_hud_pixels = sum(
        1 for y in range(hud_height) for x in range(width)
        if any(abs(win[2][(y * width + x) * 4 + channel] -
            fall[2][(y * width + x) * 4 + channel]) > CAPTURE_PIXEL_CHANNEL_DELTA
            for channel in range(3)))
    if changed_hud_pixels < MIN_CHARACTER_COURSE_HUD_CHANGED_PIXELS:
        return "Character Course win and fall captures do not show a changed outcome HUD."
    return None


def write_physics_mesh_fixture(project: Path) -> None:
    """Create a tiny valid cooked tetrahedron for the native physics API smoke."""
    positions = (
        0.0, 0.0, 0.0,
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0,
    )
    indices = (0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3)
    encode = lambda values, fmt: base64.b64encode(struct.pack(fmt, *values)).decode("ascii")
    fixture = project / "test/fixtures/physics-tetra.pkg"
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_text("\n".join((
        "format=elisa-cooked-v2",
        "source=physics-tetra.gltf",
        "source_sha256=" + "0" * 64,
        "triangles=4",
        "positions=4",
        "indices=12",
        "position_stride=12",
        "normal_stride=12",
        "uv_stride=8",
        "index_stride=4",
        "positions_b64=" + encode(positions, "<12f"),
        "normals_b64=" + encode((0.0,) * 12, "<12f"),
        "uvs_b64=" + encode((0.0,) * 8, "<8f"),
        "indices_b64=" + encode(indices, "<12I"),
        "",
    )), encoding="ascii")
    collision_source = fixture.with_name("physics-tetra.gltf")
    cook_physics_collision.write_tetrahedron_fixture(collision_source)
    cook_physics_collision.cook_collision_package(collision_source,
        "test/fixtures/physics-tetra.gltf", fixture.with_name("physics-tetra-convex.elpk"),
        "convex_hull")
    cook_physics_collision.cook_collision_package(collision_source,
        "test/fixtures/physics-tetra.gltf", fixture.with_name("physics-tetra-triangle.elpk"),
        "triangle_mesh")


def cooked_mesh_text(name: str, positions: tuple, indices: tuple) -> bytes:
    """A minimal elisa-cooked-v2 mesh with flat up normals and zero UVs."""
    count = len(positions) // 3
    encode = lambda values, fmt: base64.b64encode(struct.pack(fmt, *values)).decode("ascii")
    return "\n".join((
        "format=elisa-cooked-v2",
        f"source={name}.gltf",
        "source_sha256=" + "0" * 64,
        f"triangles={len(indices) // 3}",
        f"positions={count}",
        f"indices={len(indices)}",
        "position_stride=12",
        "normal_stride=12",
        "uv_stride=8",
        "index_stride=4",
        "positions_b64=" + encode(positions, f"<{len(positions)}f"),
        "normals_b64=" + encode((0.0, 1.0, 0.0) * count, f"<{count * 3}f"),
        "uvs_b64=" + encode((0.0,) * (count * 2), f"<{count * 2}f"),
        "indices_b64=" + encode(indices, f"<{len(indices)}I"),
        "",
    )).encode("ascii")


def write_cell_stream_fixtures(directory: Path) -> None:
    """Cooked cell packages: two meshes shared by even and odd cells, and 13
    material packages whose first texel encodes red = x*255/12, green = 102,
    blue = 255 - red. cell-streaming-smoke writes them to `cell-stream/`;
    the character course ships the same set in `cells/` (make_cells.py)."""
    box = (
        -0.5, -0.5, -0.5, 0.5, -0.5, -0.5, 0.5, 0.5, -0.5, -0.5, 0.5, -0.5,
        -0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5, 0.5, 0.5,
    )
    box_indices = (0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
        2, 3, 7, 2, 7, 6, 1, 2, 6, 1, 6, 5, 0, 4, 7, 0, 7, 3)
    pyramid = (-0.5, 0.0, -0.5, 0.5, 0.0, -0.5, 0.5, 0.0, 0.5, -0.5, 0.0, 0.5, 0.0, 1.0, 0.0)
    pyramid_indices = (0, 1, 2, 0, 2, 3, 0, 4, 1, 1, 4, 2, 2, 4, 3, 3, 4, 0)
    write_package(directory / "cell-mesh-0.elpk", {"mesh": cooked_mesh_text("cell-box", box, box_indices)})
    write_package(directory / "cell-mesh-1.elpk",
        {"mesh": cooked_mesh_text("cell-pyramid", pyramid, pyramid_indices)})
    for x in range(13):
        red = x * 255 // 12
        texel = bytes((red, 102, 255 - red, 255))
        write_package(directory / f"cell-paint-{x:02d}.elpk", {"albedo": texel * 8})


def main() -> int:
    only_names = [name for name in os.environ.get("ELISA_NATIVE_SMOKE_ONLY", "").split(",") if name]
    if len(sys.argv) == 3 and sys.argv[1] == "--only":
        only_names = sys.argv[2].split(",")
        if any(not name for name in only_names):
            print("--only requires one or more comma-separated project names", file=sys.stderr)
            return 2
    elif len(sys.argv) != 1:
        print("usage: application_native_smoke.py [--only NAME[,NAME...]]", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="Elisa application smoke ") as temporary_directory:
        project = Path(temporary_directory)
        fixture = project / "test/fixtures/audio-smoke.wav"
        fixture.parent.mkdir(parents=True, exist_ok=True)
        write_physics_mesh_fixture(project)
        write_cell_stream_fixtures(project / "cell-stream")
        # The course self-test decodes its shipped clips from `sounds/` and
        # animates two instances of `rigs/guide_rig.pkg`; captions come from `text/`, the Arabic fallback font from `fonts/`.
        for resource in ("sounds", "rigs", "text", "fonts", "cells"):
            shutil.copytree(ROOT / "examples/character_course" / resource, project / resource)
        samples = [int(6000 * math.sin(2.0 * math.pi * 440.0 * frame / 8000)) for frame in range(400)]
        with wave.open(str(fixture), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8000)
            output.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))
        runner = ROOT / "scripts/elisa_build_run.py"
        settings = {
            "title": "Elisa Engine Smoke",
            "width": 320,
            "height": 200,
            "hidden": True,
        }
        projects = [
            ("physics-pose-inertia-smoke", ROOT / "test/physics_inertia_native_main.elisa"),
            ("physics-material-smoke", ROOT / "test/physics_material_native_main.elisa"),
            ("physics-ccd3d-smoke", ROOT / "test/physics_ccd3d_native_main.elisa"),
            ("world-physics-pose-smoke", ROOT / "test/world_physics_pose_native_main.elisa"),
            ("render-resource-churn-smoke", ROOT / "test/render_resource_churn_native_main.elisa"),
            ("cell-streaming-smoke", ROOT / "test/cell_streaming_native_main.elisa"),
            ("render-many-instances-smoke", ROOT / "test/render_many_instances_native_main.elisa"),
            ("world-save-physics-smoke", ROOT / "test/world_save_physics_native_main.elisa"),
            ("world-physics-cadence-smoke",
                ROOT / "test/world_physics_session_cadence_native_main.elisa"),
            ("world-hierarchy-render-smoke", ROOT / "test/world_hierarchy_render_native_main.elisa"),
            ("world-picking-smoke", ROOT / "test/world_picking_native_main.elisa"),
            ("bounds-view-smoke", ROOT / "test/bounds_view_native_main.elisa"),
            ("world-audio-physics-smoke", ROOT / "test/world_audio_physics_native_main.elisa"),
            ("world-audio-scale-smoke", ROOT / "test/world_audio_scale_native_main.elisa"),
            ("quality-settings-native-smoke", ROOT / "test/quality_settings_native_main.elisa"),
            ("physics-primitives-smoke", ROOT / "test/physics_primitives_native.elisa"),
            ("physics-runtime-query-smoke", ROOT / "test/physics_app_native.elisa"),
            ("physics-constraints-smoke", ROOT / "test/physics_constraints_native.elisa"),
            ("physics-interactables-smoke", ROOT / "examples/physics_interactables/self_test_main.elisa"),
            ("character-course-smoke", ROOT / "examples/character_course/self_test_main.elisa"),
            # A second process reloads the saves the course self-test leaves in
            # the shared user-data directory, so it must run right after it.
            ("character-course-relaunch-smoke", ROOT / "examples/character_course/relaunch_main.elisa"),
            # The real course loop, piloted by pushed key events across streamed floor cells.
            ("character-course-cells-smoke", ROOT / "examples/character_course/stream_test_main.elisa"),
            # The real game loop, with the controller driven to the summit through SDL events.
            ("character-course-live-input-smoke",
                ROOT / "examples/character_course/live_input_test_main.elisa"),
            ("physics-collision-layers-smoke", ROOT / "test/physics_collision_layers_native.elisa"),
            ("physics-mesh-shapes-smoke", ROOT / "test/physics_mesh_shapes_native.elisa"),
            ("physics-render-capture-smoke", ROOT / "test/physics_render_capture_native.elisa"),
            ("application-native-smoke", ROOT / "test/application_native_main.elisa"),
            ("application-frame-time-smoke", ROOT / "test/application_frame_time_native.elisa"),
            ("application-gamepad-rumble-smoke",
                ROOT / "test/application_gamepad_rumble_native.elisa"),
            ("application-async-capture-smoke", ROOT / "test/application_capture_async_main.elisa"),
            ("application-error-message-smoke", ROOT / "test/application_error_message_native.elisa"),
            ("application-failure-cleanup-smoke", ROOT / "test/application_failure_native_main.elisa"),
        ]
        if only_names:
            known_names = [project_row[0] for project_row in projects]
            unknown_names = [name for name in only_names if name not in known_names]
            if unknown_names:
                print("Unknown native smoke project: " + ", ".join(unknown_names), file=sys.stderr)
                return 2
            projects = [project_row for project_row in projects if project_row[0] in only_names]
            if "character-course-relaunch-smoke" in only_names and "character-course-smoke" not in only_names:
                print("character-course-relaunch-smoke needs character-course-smoke first", file=sys.stderr)
                return 2
        physics_captures = ROOT / "build/validation/physics-render-cadence"
        physics_captures.mkdir(parents=True, exist_ok=True)
        character_course_captures = ROOT / "build/validation/character-course-presentation"
        character_course_win_capture = character_course_captures / "win.png"
        character_course_fall_capture = character_course_captures / "fall.png"
        if any(name == "character-course-live-input-smoke" for name, _ in projects):
            character_course_captures.mkdir(parents=True, exist_ok=True)
            character_course_win_capture.unlink(missing_ok=True)
            character_course_fall_capture.unlink(missing_ok=True)
        frame_capture_dirs = {
            "30hz": physics_captures / "physics-30hz-frames",
            "120hz": physics_captures / "physics-120hz-frames",
        }
        for frame_capture_dir in frame_capture_dirs.values():
            shutil.rmtree(frame_capture_dir, ignore_errors=True)
            frame_capture_dir.mkdir(parents=True, exist_ok=True)
        for capture_name in (
                "physics-30hz.png", "physics-120hz.png",
                "physics-30hz-mid.png", "physics-120hz-mid.png",
                "hierarchy-before.png", "hierarchy-after.png"):
            (physics_captures / capture_name).unlink(missing_ok=True)
        native_test = project / "user-data-native-test"
        native_command = [
            "clang++", "-std=c++17", "-O0",
            str(ROOT / "native/user_data_abi.cpp"),
            str(ROOT / "test/user_data_service_test.cpp"),
            "-o", str(native_test),
        ]
        compiled = subprocess.run(native_command, check=False)
        if compiled.returncode != 0:
            print("Native user-data service test did not compile.", file=sys.stderr)
            return compiled.returncode
        tested = subprocess.run([str(native_test)], check=False)
        if tested.returncode != 0:
            print("Native user-data service test failed.", file=sys.stderr)
            return tested.returncode

        for name, entry in projects:
            if name == "physics-mesh-shapes-smoke":
                shutil.rmtree(project / "build/cache/physics", ignore_errors=True)
            manifest = {
                "name": name,
                "main": str(entry),
                "output": f"build/{name}",
                "application": settings,
            }
            (project / "elisa.project.json").write_text(json.dumps(manifest), encoding="utf-8")
            command = [sys.executable, str(runner), "run", "--project", str(project),
                "--native-test-probes"]
            # These fixtures include their runtime modules explicitly; avoid
            # importing the whole public bundle a second time through the wrapper.
            if name in ("world-hierarchy-render-smoke", "world-save-physics-smoke"):
                command.append("--no-public-runtime")
            environment = native_smoke_environment(os.environ,
                allow_device_reopen=name == "character-course-relaunch-smoke")
            environment["ELISA_USER_DATA_DIR"] = str(project / "user-data")
            environment["ELISA_PROJECT_ROOT"] = str(project)
            screenshot = project / f"{name}-frame.png"
            async_screenshot = project / f"{name}-async-frame.png"
            environment["ELISA_SMOKE_SCREENSHOT_PATH"] = str(screenshot)
            environment["ELISA_ASYNC_SMOKE_SCREENSHOT_PATH"] = str(async_screenshot)
            environment["ELISA_PHYSICS_30HZ_CAPTURE_PATH"] = str(physics_captures / "physics-30hz.png")
            environment["ELISA_PHYSICS_120HZ_CAPTURE_PATH"] = str(physics_captures / "physics-120hz.png")
            environment["ELISA_PHYSICS_30HZ_MID_CAPTURE_PATH"] = str(physics_captures / "physics-30hz-mid.png")
            environment["ELISA_PHYSICS_120HZ_MID_CAPTURE_PATH"] = str(physics_captures / "physics-120hz-mid.png")
            environment["ELISA_PHYSICS_30HZ_FRAME_CAPTURE_DIR"] = str(frame_capture_dirs["30hz"])
            environment["ELISA_PHYSICS_120HZ_FRAME_CAPTURE_DIR"] = str(frame_capture_dirs["120hz"])
            environment["ELISA_HIERARCHY_BEFORE_CAPTURE_PATH"] = str(physics_captures / "hierarchy-before.png")
            environment["ELISA_HIERARCHY_AFTER_CAPTURE_PATH"] = str(physics_captures / "hierarchy-after.png")
            if name == "character-course-live-input-smoke":
                environment["ELISA_CHARACTER_COURSE_WIN_CAPTURE_PATH"] = str(character_course_win_capture)
                environment["ELISA_CHARACTER_COURSE_FALL_CAPTURE_PATH"] = str(character_course_fall_capture)
            started = time.monotonic()
            completed = subprocess.run(command, env=environment, check=False,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
            sys.stdout.write(completed.stdout)
            sys.stdout.flush()
            status = completed.returncode
            output = completed.stdout
            if status == 0 and name == "character-course-live-input-smoke":
                capture_error = character_course_presentation_error(
                    character_course_win_capture, character_course_fall_capture)
                if capture_error is None:
                    capture_note = ("Validated Character Course win/fall presentation captures at "
                        f"{character_course_win_capture} and {character_course_fall_capture}.")
                    print(capture_note)
                    output += capture_note + "\n"
                else:
                    print(capture_error, file=sys.stderr)
                    status = 1
                    output += capture_error + "\n"
            artifact = native_smoke_artifacts.record(ROOT / "build/native-smoke", name, Path(entry), status,
                time.monotonic() - started, output)
            if status == 0 and name == "application-native-smoke":
                header = screenshot.read_bytes()[:8] if screenshot.exists() else b""
                if header != PNG_SIGNATURE:
                    print("Native application smoke did not write a PNG screenshot.", file=sys.stderr)
                    return 1
            if status == 0 and name == "application-async-capture-smoke":
                async_screenshot = Path(environment["ELISA_ASYNC_SMOKE_SCREENSHOT_PATH"])
                async_image = decode_capture_png(async_screenshot.read_bytes()) if async_screenshot.exists() else None
                if async_image is None or async_image[0] <= 0 or async_image[1] <= 0:
                    print("Asynchronous capture smoke did not write a valid RGBA PNG.", file=sys.stderr)
                    return 1
                expected_black_pixels = async_image[0] * async_image[1]
                if async_image[2].count(b"\x00\x00\x00\xff") != expected_black_pixels:
                    print("Asynchronous capture differs from the empty-frame RGBA reference.", file=sys.stderr)
                    return 1
                print(f"Resize-safe asynchronous capture decoded at {async_image[0]}x{async_image[1]} pixels.")
            if status == 0 and name == "physics-render-capture-smoke":
                capture_pairs = (
                    ("midpoint", physics_captures / "physics-30hz-mid.png",
                        physics_captures / "physics-120hz-mid.png"),
                    ("final", physics_captures / "physics-30hz.png",
                        physics_captures / "physics-120hz.png"),
                )
                for label, thirty_path, one_twenty_path in capture_pairs:
                    image_data = []
                    image_bytes = []
                    image_sizes = []
                    for cadence_path in (thirty_path, one_twenty_path):
                        data = cadence_path.read_bytes() if cadence_path.exists() else b""
                        decoded = decode_capture_png(data)
                        if decoded is None:
                            print(f"Physics render cadence smoke did not write a valid {cadence_path.name} PNG.", file=sys.stderr)
                            return 1
                        width, height, pixels = decoded
                        colored_pixels = sum(1 for index in range(0, len(pixels), 4)
                            if max(pixels[index:index + 3]) > MIN_VISIBLE_CHANNEL_VALUE)
                        if colored_pixels < MIN_VISIBLE_CAPTURE_PIXELS:
                            print(f"Physics cadence capture {cadence_path.name} is visually empty.", file=sys.stderr)
                            return 1
                        image_sizes.append((width, height))
                        image_data.append(pixels)
                        image_bytes.append(data)
                    if (image_sizes[0][0] <= 0 or image_sizes[0][1] <= 0 or
                            image_sizes[0] != image_sizes[1]):
                        print(f"Physics {label} captures have invalid or mismatched dimensions.", file=sys.stderr)
                        return 1
                    if image_bytes[0] != image_bytes[1]:
                        print(f"30 Hz and 120 Hz physics-to-Wicked {label} PNGs differ byte-for-byte.", file=sys.stderr)
                        return 1
                    if image_data[0] != image_data[1]:
                        print(f"30 Hz and 120 Hz physics-to-Wicked {label} pixels differ.", file=sys.stderr)
                        return 1
                    print(f"Physics-to-Wicked {label} captures match pixel-for-pixel at {image_sizes[0][0]}x{image_sizes[0][1]} pixels.")
                expected_frames = {f"frame-{index}.png" for index in range(30)}
                for cadence, frame_capture_dir in frame_capture_dirs.items():
                    actual_frames = {capture.name for capture in frame_capture_dir.glob("*.png")}
                    if actual_frames != expected_frames:
                        print(f"Physics {cadence} run captured {len(actual_frames)} of 30 matching frames.", file=sys.stderr)
                        return 1
                for frame_index in range(30):
                    thirty_path = frame_capture_dirs["30hz"] / f"frame-{frame_index}.png"
                    one_twenty_path = frame_capture_dirs["120hz"] / f"frame-{frame_index}.png"
                    thirty = decode_capture_png(thirty_path.read_bytes())
                    one_twenty = decode_capture_png(one_twenty_path.read_bytes())
                    if (thirty is None or one_twenty is None or thirty[:2] != one_twenty[:2]):
                        print(f"Physics cadence frame {frame_index} has invalid or mismatched captures.", file=sys.stderr)
                        return 1
                    if thirty[2] != one_twenty[2]:
                        print(f"30 Hz and 120 Hz frame {frame_index} pixels differ.", file=sys.stderr)
                        return 1
                print("All 30 shared-time 30 Hz and 120 Hz physics-to-Wicked frames match pixel-for-pixel.")
            if status == 0 and name in (
                    "world-physics-pose-smoke", "world-hierarchy-render-smoke"):
                before_path = physics_captures / "hierarchy-before.png"
                after_path = physics_captures / "hierarchy-after.png"
                before = decode_capture_png(before_path.read_bytes()) if before_path.exists() else None
                after = decode_capture_png(after_path.read_bytes()) if after_path.exists() else None
                if before is None or after is None:
                    print("Hierarchy-to-Wicked smoke did not write two valid RGBA PNGs.", file=sys.stderr)
                    return 1
                if before[0] <= 0 or before[1] <= 0 or after[:2] != before[:2]:
                    print("Hierarchy-to-Wicked captures have unexpected dimensions.", file=sys.stderr)
                    return 1
                for label, pixels in (("before", before[2]), ("after", after[2])):
                    colored_pixels = sum(1 for index in range(0, len(pixels), 4)
                        if max(pixels[index:index + 3]) > MIN_VISIBLE_CHANNEL_VALUE)
                    if colored_pixels < MIN_HIERARCHY_CAPTURE_PIXELS:
                        print(f"Hierarchy {label} capture is visually empty.", file=sys.stderr)
                        return 1
                changed_pixels = sum(1 for index in range(0, len(before[2]), 4)
                    if any(abs(before[2][index + channel] - after[2][index + channel]) > 8
                        for channel in range(3)))
                if changed_pixels < 128:
                    print(f"Hierarchy sample changed only {changed_pixels} visible Wicked pixels.", file=sys.stderr)
                    return 1
                print(f"Hierarchy-to-Wicked sample moved across {changed_pixels} pixels at {before[0]}x{before[1]}.")
            if status != 0:
                print(f"Native application smoke {name} failed with status {status}; see {artifact}.", file=sys.stderr)
                return status
            print(f"Native application smoke {name} passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
