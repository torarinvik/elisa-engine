#!/usr/bin/env python3
"""Keep automated application launches silent even when the user has audio enabled."""

from __future__ import annotations

import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from application_native_smoke import (
    MIN_CHARACTER_COURSE_HUD_CHANGED_PIXELS,
    character_course_presentation_error,
    character_course_menu_error,
    native_smoke_environment,
)
from packaged_maze_smoke import application_environment as packaged_maze_environment
from validate_standalone_macos_app import launch_environment as standalone_launch_environment


def rgba_png(width: int, height: int, pixels: bytes) -> bytes:
    def chunk(name: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + name + payload +
            struct.pack(">I", zlib.crc32(name + payload) & 0xFFFFFFFF))

    scanlines = b"".join(b"\x00" + pixels[row * width * 4:(row + 1) * width * 4]
        for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
        chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


class ApplicationNativeSmokeTests(unittest.TestCase):
    def test_launch_environment_forces_silent_audio_provider(self) -> None:
        environment = native_smoke_environment({
            "ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE": "0",
            "ELISA_PROJECT_ROOT": "/tmp/project",
            "PATH": "/usr/bin",
        })

        self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], "1")
        self.assertEqual(environment["ELISA_PROJECT_ROOT"], "/tmp/project")
        self.assertEqual(environment["PATH"], "/usr/bin")

    def test_course_relaunch_reopens_saved_device_by_default(self) -> None:
        environment = native_smoke_environment(
            {"ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE": "0"},
            allow_device_reopen=True)

        self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], "0")

    def test_course_relaunch_preserves_native_force_value_semantics(self) -> None:
        cases = (
            ("1", "1"),
            ("true", "1"),
            ("false", "1"),  # Native semantics are nonempty and not prefixed by 0.
            ("", "0"),
            ("0", "0"),
            ("0false", "0"),
        )
        for value, expected in cases:
            with self.subTest(value=value):
                environment = native_smoke_environment(
                    {"ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE": value},
                    allow_device_reopen=True)

                self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], expected)

    def test_relocated_package_launch_is_silent_and_isolated(self) -> None:
        environment = standalone_launch_environment(Path("/tmp/isolated-home"), {
            "ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE": "0",
            "ELISA_PROJECT_ROOT": "/private/project",
            "WICKED_ROOT": "/private/wicked",
            "DYLD_LIBRARY_PATH": "/private/libraries",
            "PATH": "/usr/bin",
        })

        self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], "1")
        self.assertEqual(environment["HOME"], "/tmp/isolated-home")
        self.assertEqual(environment["PATH"], "/usr/bin")
        self.assertNotIn("ELISA_PROJECT_ROOT", environment)
        self.assertNotIn("WICKED_ROOT", environment)
        self.assertNotIn("DYLD_LIBRARY_PATH", environment)

    def test_packaged_maze_smoke_forces_silent_audio(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            project = Path(temporary_directory)
            (project / "elisa.project.json").write_text(
                json.dumps({"application": {"title": "Fixture"}}), encoding="utf-8")
            environment = packaged_maze_environment(
                project, project / "staged", project / "shaders")

        self.assertEqual(environment["ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE"], "1")

    def test_character_course_outcomes_require_visible_distinct_hud_captures(self) -> None:
        width, height = 12, 100
        win_pixels = bytes((32, 32, 32, 255)) * width * height
        fall_pixels = bytearray(win_pixels)
        for pixel in range(MIN_CHARACTER_COURSE_HUD_CHANGED_PIXELS):
            fall_pixels[pixel * 4] = 96
        with tempfile.TemporaryDirectory() as temporary_directory:
            win_path = Path(temporary_directory) / "win.png"
            fall_path = Path(temporary_directory) / "fall.png"
            win_path.write_bytes(rgba_png(width, height, win_pixels))
            fall_path.write_bytes(rgba_png(width, height, bytes(fall_pixels)))
            self.assertIsNone(character_course_presentation_error(win_path, fall_path))

            fall_path.write_bytes(rgba_png(width, height, win_pixels))
            self.assertIn("changed outcome HUD",
                character_course_presentation_error(win_path, fall_path) or "")

    def test_course_menu_requires_all_changed_small_window_captures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            directory = Path(temporary_directory)
            self.assertIn("Missing", character_course_menu_error(directory) or "")
            for index, name in enumerate(("100", "125", "150", "bottom")):
                pixels = bytes((240 + index, 240 + index, 240 + index, 255)) * 600 * 400
                (directory / f"menu-{name}.png").write_bytes(rgba_png(600, 400, pixels))
            self.assertIsNone(character_course_menu_error(directory))
            first_image = (directory / "menu-100.png").read_bytes()
            (directory / "menu-100.png").write_bytes(rgba_png(600, 400, bytes((32, 32, 32, 255)) * 600 * 400))
            self.assertIn("bright text", character_course_menu_error(directory) or "")
            (directory / "menu-100.png").write_bytes(first_image)
            (directory / "menu-125.png").write_bytes((directory / "menu-100.png").read_bytes())
            self.assertIn("did not change", character_course_menu_error(directory) or "")
            (directory / "menu-125.png").write_bytes(rgba_png(600, 300, bytes((32, 32, 32, 255)) * 600 * 300))
            self.assertIn("dimensions", character_course_menu_error(directory) or "")


if __name__ == "__main__":
    unittest.main()
